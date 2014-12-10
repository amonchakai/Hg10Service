/*
 * XMPPController.cpp
 *
 *  Created on: 12 oct. 2014
 *      Author: pierre
 */


#include "XMPPService.hpp"
#include "QXmppMessage.h"
#include "client/QXmppConfiguration.h"
#include "client/QXmppRosterManager.h"
#include "QXmppVCardIq.h"
#include "client/QXmppVCardManager.h"
#include "../../Hg10/src/DataObjects.h"
#include <QDir>
#include <QFile>
#include <QBuffer>
#include <QDebug>
#include <QRegExp>
#include <QThread>
#include <QReadWriteLock>
#include <bb/cascades/ImageTracker>
#include <QtNetwork/QTcpSocket>
#include <QtNetwork/QTcpServer>
#include <bb/platform/Notification>
#include "client/QXmppMucManager.h"
#include <QSettings>
#include <bb/Application>

#include "GoogleConnectController.hpp"
#include "PrivateAPIKeys.h"


XMPP* XMPP::m_This = NULL;
QReadWriteLock  mutex;
QReadWriteLock  mutexLoadLocal;

// ===============================================================================================
// Utility class: start the TCP server which wait for UI in another thread

class TcpThreadBind : public QThread {

public:
    TcpThreadBind(QObject *parent = 0);
    virtual ~TcpThreadBind() {}

    int                             m_Port;
    boost::shared_ptr<QTcpServer>   m_Server;

    void run();
};

TcpThreadBind::TcpThreadBind(QObject *parent) : QThread(parent), m_Port(0) {

}

void TcpThreadBind::run() {
    if(m_Server != NULL) {
        //if(!m_Server->isListening())
         m_Server->listen(QHostAddress::LocalHost, m_Port);
        exec();
    }
}


// ===============================================================================================
// The Core XMPP Service : a QMPPClient


XMPP::XMPP(QObject *parent) : QXmppClient(parent),
        m_Connected(false),
        m_LastError(0),
        m_SendContactWhenAvailable(false),
        m_VcardManagerConnected(false),
        m_Port(27015),
        m_NotificationEnabled(true),
        m_Restarting(false) {




    bool check = connect(this, SIGNAL(messageReceived(QXmppMessage)), this, SLOT(messageReceived(QXmppMessage)));
    Q_ASSERT(check);

    check = connect(&this->rosterManager(), SIGNAL(rosterReceived()), this, SLOT(rosterReceived()));
    Q_ASSERT(check);
    Q_UNUSED(check);


    check = connect(this, SIGNAL(connected()), this, SLOT(logConnection()));
    Q_ASSERT(check);
    Q_UNUSED(check);

    check = connect(this, SIGNAL(error(QXmppClient::Error)), this, SLOT(logConnectionError(QXmppClient::Error)));
    Q_ASSERT(check);


    check = connect(&this->rosterManager(), SIGNAL(presenceChanged(QString,QString)), SLOT(presenceChanged(QString,QString)));
    Q_ASSERT(check);


    // ---------------------------------------------------------------------
    // communication controls
    m_Server = boost::shared_ptr<QTcpServer>(new QTcpServer(this));

    bool ok = connect(m_Server.get(), SIGNAL(newConnection()), this, SLOT(newConnection()));
    Q_ASSERT(ok);
    Q_UNUSED(ok);

    // put the handling of the server into a thread --> avoid blocking the XMPP client while listening a new UI client...
    m_TcpThreadBind = boost::shared_ptr<TcpThreadBind>(new TcpThreadBind());
    m_TcpThreadBind->m_Server = m_Server;
    m_TcpThreadBind->m_Port   = m_Port;






    // ---------------------------------------------------------------------
    // connection using oauth (PREFERED)

    QSettings settings("Amonchakai", "Hg10");
    if(!settings.value("access_token").value<QString>().isEmpty()) {

        m_GoogleConnect = boost::shared_ptr<GoogleConnectController>(new GoogleConnectController());
        bool check = connect(m_GoogleConnect.get(), SIGNAL(tokenObtained(const QString&)), this, SLOT(readyRestart(const QString &)));
        Q_ASSERT(check);
        Q_UNUSED(check);

        check = connect(m_GoogleConnect.get(), SIGNAL(failedRenew()), this, SLOT(oauth2Restart()));
        Q_ASSERT(check);

        check = connect(this, SIGNAL(disconnected()), this, SLOT(oauthDisconnected()));
        Q_ASSERT(check);

        oauth2Restart();

    } else {






    // ---------------------------------------------------------------------
    // connection using user ID/PASSWORD (LEGACY)


        QString directory = QDir::homePath() + QLatin1String("/ApplicationData");
        if (!QFile::exists(directory)) {
            return;
        }

        QFile file(directory + "/UserID.txt");
        if (file.open(QIODevice::ReadOnly)) {
            QDataStream stream(&file);
            QString password, host, domain;
            int port, encryption;
            stream >> m_User;
            stream >> password;

            stream >> host;
            stream >> domain;
            stream >> port;
            stream >> encryption;

            if(!password.isEmpty()) {
                if(host.isEmpty())
                    connectToServer(m_User, password);
                else {
                    QXmppConfiguration configuration;
                    configuration.setHost(host);
                    configuration.setDomain(domain);
                    configuration.setPort(port);
                    configuration.setUser(m_User);
                    configuration.setPassword(password);
                    configuration.setAutoReconnectionEnabled(true);

                    switch(encryption) {
                        case 0:
                            configuration.setStreamSecurityMode(QXmppConfiguration::TLSEnabled);
                            break;
                        case 1:
                            configuration.setStreamSecurityMode(QXmppConfiguration::TLSDisabled);
                            break;
                        case 2:
                           configuration.setStreamSecurityMode(QXmppConfiguration::TLSRequired);
                           break;
                        }

                    connectToServer(configuration);
                }
            }

            file.close();
        }
    }

    waitRemote();

}






// -----------------------------------------------------------------------------------------------
// The XMPP client is a singleton

// get singleton
XMPP *XMPP::get() {
    if(m_This == NULL) {
        // if not already done, instantiate the network manager
        m_This = new XMPP(NULL);
    }

    return m_This;
}






// -----------------------------------------------------------------------------------------------
// Handle issue about connection:
// - (1) auto start when headless start (device reboot, headless crash, ...)
// - (2) auto restart after losing connection to server (loss of network, connection die)
// - (3) start connection on request

// case 2:
void XMPP::oauthDisconnected() {
    qDebug() << "disconnect & reconnect";
    disconnectFromServer();
    m_Connected = false;

    logFailedConnecting();

    oauth2Restart();
}


// restart server, we may need to ask for a token
// case 1 & 2:
void XMPP::oauth2Restart() {
    qDebug() << "try to renew Token!";
    if(m_Restarting)
        return;

    QSettings settings("Amonchakai", "Hg10");
    if(settings.value("access_token").toString().isEmpty())
        return;

    m_Restarting = true;
    QTimer::singleShot(3000, this, SLOT(askNewToken()));
}

// if token asked wait reply before asking a new token
void XMPP::askNewToken() {
    m_Restarting = false;
    m_GoogleConnect->renewToken();
}

// the token is available, retart!
void XMPP::readyRestart(const QString &token) {
    qDebug() << "Ready to restart!";
    m_Restarting = false;

    QSettings settings("Amonchakai", "Hg10");
    m_User = settings.value("User").toString();

    QXmppConfiguration configuration;
    configuration.setHost("talk.google.com");
    configuration.setStreamSecurityMode(QXmppConfiguration::TLSRequired);
    configuration.setDomain("gmail.com");
    configuration.setPort(5222);
    configuration.setUser(m_User.mid(0, m_User.indexOf("@")));
    configuration.setGoogleAccessToken(token);
    configuration.setAutoReconnectionEnabled(true);

    connectToServer(configuration);

}

// case 3:
// Receive order from view to start a connection
void XMPP::oauth2Login(const QString &user) {

    QSettings settings("Amonchakai", "Hg10");

    m_User = user;

    if(!settings.value("access_token").value<QString>().isEmpty()) {
        QXmppConfiguration configuration;
        configuration.setHost("talk.google.com");
        configuration.setStreamSecurityMode(QXmppConfiguration::TLSRequired);
        configuration.setDomain("gmail.com");
        configuration.setPort(5222);
        configuration.setUser(user.mid(0, user.indexOf("@")));
        configuration.setGoogleAccessToken(settings.value("access_token").value<QString>());
        configuration.setAutoReconnectionEnabled(true);

        connectToServer(configuration);

        return;
    }

    if(!settings.value("Facebook_access_token").value<QString>().isEmpty()) {

        qDebug() << "TOKEN: " << settings.value("Facebook_access_token").value<QString>();
        qDebug() << "USER: " << settings.value("Facebook_userid").value<QString>();

        QXmppConfiguration configuration;
        configuration.setDomain("chat.facebook.com");
//        configuration.setHost("chat.facebook.com");
        configuration.setNonSASLAuthMechanism(QXmppConfiguration::NonSASLPlain);
        configuration.setPort(5222);
        configuration.setStreamSecurityMode(QXmppConfiguration::TLSRequired);
//        configuration.setUser(settings.value("Facebook_userid").value<QString>());
        configuration.setFacebookAccessToken(settings.value("Facebook_access_token").value<QString>());
        configuration.setFacebookAppId(FACEBOOK_CIENT_ID);
        configuration.setAutoReconnectionEnabled(true);

        logger()->setLoggingType(QXmppLogger::StdoutLogging);
        logger()->setMessageTypes(QXmppLogger::AnyMessage);

        connectToServer(configuration);
    }
}








// -----------------------------------------------------------------------------------------------
// Handle settings


void XMPP::updateSettings() {
    QSettings settings("Amonchakai", "Hg10");
    m_NotificationEnabled = settings.value("notifications", true).value<bool>();
}









// -----------------------------------------------------------------------------------------------
// Send connection status to the view

// succeeded
void XMPP::logConnection() {
    mutex.lockForWrite();
    m_Connected = true;
    if (m_Socket && m_Socket->state() == QTcpSocket::ConnectedState) {
        m_LastError = 0;
        int code = XMPPServiceMessages::REPLY_LOGGED_IN;
        m_Socket->write(reinterpret_cast<char *>(&code), sizeof(int));
        m_Socket->flush();
    }
    mutex.unlock();
}

// failed
void XMPP::logFailedConnecting() {
    mutex.lockForWrite();
    if (m_Socket && m_Socket->state() == QTcpSocket::ConnectedState) {
        m_LastError = 0;
        int code = XMPPServiceMessages::REPLY_CONNECTION_FAILED;
        m_Socket->write(reinterpret_cast<char *>(&code), sizeof(int));
        m_Socket->flush();
    }
    mutex.unlock();
}








// -----------------------------------------------------------------------------------------------
// Connection status


void XMPP::logConnectionError(QXmppClient::Error error) {
    if(error == QXmppClient::NoError)
        return;

    if(m_LastError >= 0)
        return;

    m_LastError = error;
    qDebug() << "Connection Error: " << error;

    if (m_Socket && m_Socket->state() == QTcpSocket::ConnectedState) {
        int code = XMPPServiceMessages::REPLY_CONNECTION_FAILED;
        m_Socket->write(reinterpret_cast<char *>(&code), sizeof(int));

        m_Socket->flush();
    }

}








// ===============================================================================================
// Chat messages





void XMPP::messageReceived(const QXmppMessage& message) {
    mutex.lockForWrite();

    if (m_Socket && m_Socket->state() == QTcpSocket::ConnectedState) {
        if(message.body().isEmpty()) {
            int code = XMPPServiceMessages::STATUS_UPDATE;
            m_Socket->write(reinterpret_cast<char *>(&code), sizeof(int));

            int length = message.from().length();
            m_Socket->write(reinterpret_cast<char *>(&length), sizeof(int));
            m_Socket->write(message.from().toAscii(), length);

            code = message.state();
            m_Socket->write(reinterpret_cast<char *>(&code), sizeof(int));
            m_Socket->flush();
        } else {

            int code = XMPPServiceMessages::MESSAGE;
            m_Socket->write(reinterpret_cast<char *>(&code), sizeof(int));

            int length = message.from().length();
            m_Socket->write(reinterpret_cast<char *>(&length), sizeof(int));
            m_Socket->write(message.from().toAscii(), length);

            length = message.to().length();
            m_Socket->write(reinterpret_cast<char *>(&length), sizeof(int));
            m_Socket->write(message.to().toAscii(), length);

            length = message.body().length();
            m_Socket->write(reinterpret_cast<char *>(&length), sizeof(int));
            m_Socket->write(message.body().toAscii(), length);
            m_Socket->flush();

        }
    } else {
        if(!message.body().isEmpty()) {

            if(m_NotificationEnabled) {
                bb::platform::Notification notif;
                notif.notify();
            }

            logReceivedMessage(message.from(), message.to(), message.body());
        }

    }

    mutex.unlock();

}


void XMPP::presenceChanged(const QString& bareJid, const QString& resource) {
    mutex.lockForWrite();

    int status = rosterManager().getPresence(bareJid, resource).availableStatusType();
    m_ContactList[bareJid] = status;

    if (m_Socket && m_Socket->state() == QTcpSocket::ConnectedState) {
        int code = XMPPServiceMessages::PRESENCE_UPDATE;
        m_Socket->write(reinterpret_cast<char *>(&code), sizeof(int));

        int length = bareJid.length();
        m_Socket->write(reinterpret_cast<char *>(&length), sizeof(int));
        m_Socket->write(bareJid.toAscii(), length);

        m_Socket->write(reinterpret_cast<char *>(&status), sizeof(int));
        m_Socket->flush();
    }
    mutex.unlock();

}

void XMPP::sendContactsPersence() {
    mutex.lockForWrite();

    if (m_Socket && m_Socket->state() == QTcpSocket::ConnectedState) {
        for(QMap<QString, int>::iterator it = m_ContactList.begin() ; it != m_ContactList.end() ; ++it) {
            int code = XMPPServiceMessages::PRESENCE_UPDATE;
            m_Socket->write(reinterpret_cast<char *>(&code), sizeof(int));

            int length = it.key().length();
            m_Socket->write(reinterpret_cast<char *>(&length), sizeof(int));
            m_Socket->write(it.key().toAscii(), length);

            int status = it.value();
            m_Socket->write(reinterpret_cast<char *>(&status), sizeof(int));

        }
        m_Socket->flush();
    }
    mutex.unlock();
}


void XMPP::rosterReceived() {
    mutexLoadLocal.lockForWrite();

    if(!m_VcardManagerConnected) {
        bool check = connect(&this->vCardManager(), SIGNAL(vCardReceived(QXmppVCardIq)), this, SLOT(vCardReceived(QXmppVCardIq)));

        Q_ASSERT(check);
        Q_UNUSED(check);

        m_VcardManagerConnected = true;
    }

    QStringList list = rosterManager().getRosterBareJids();


    vCardManager().requestClientVCard();

    QString vCardsDir = QDir::homePath() + QLatin1String("/vCards");


    for(int i = 0; i < list.size(); ++i) {
        QMap<QString, int>::iterator it = m_ContactList.find(list.at(i));
        if(it == m_ContactList.end())
            m_ContactList[list.at(i)] = -1;

        // request vCard of all the bareJids in roster
        if(!QFile::exists(vCardsDir + "/" + list.at(i) + ".xml")) {
            qDebug() << "request: " << list.at(i);
            vCardManager().requestVCard(list.at(i));
        } else {
            // if card already exists, then no need to request it.
            mutex.lockForWrite();
            sendContact(list.at(i));
            mutex.unlock();
        }
    }

    mutexLoadLocal.unlock();
    emit offline(false);
}

void XMPP::vCardReceived(const QXmppVCardIq& vCard) {
    QString bareJid = vCard.from();
    QString vCardsDir = QDir::homePath() + QLatin1String("/vCards");
    QRegExp isFacebook("(.*)@chat.facebook.com");

    if(bareJid.isEmpty() && vCard.fullName().isEmpty())
        return;

    if(bareJid.isEmpty())
        bareJid = m_User;

    QDir dir;
    if(!dir.exists(vCardsDir))
        dir.mkdir(vCardsDir);

    QFile file(vCardsDir + "/" + bareJid + ".xml");
    if(file.open(QIODevice::ReadWrite))
    {
        QXmlStreamWriter stream(&file);
        vCard.toXml(&stream);
        file.close();
    }

    if(isFacebook.indexIn(bareJid) == -1) {

        // ----------------------------------------------------------------------------------
        // two options to get the picture: either it comes from XMPP or from Facebook.
        // here it is from XMPP


        QString name(vCardsDir + "/" + bareJid + ".png");

        QByteArray photo = vCard.photo();
        QImage qImage;
        qImage.loadFromData(vCard.photo());


        if(!qImage.isNull() && qImage.size().height() > qImage.size().width()) {
            QImage nqImage = qImage.scaled(qImage.size().height(), qImage.size().height(), Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
            qImage = nqImage;
        }

        if(!qImage.isNull() && qImage.size().height() < 64 && qImage.size().width()) {
            //QPixmap pixMap = QPixmap::fromImage(qImage.convertToFormat(QImage::Format_ARGB4444_Premultiplied)); /*
            QImage nqImage = qImage.scaled(100, 100, Qt::KeepAspectRatio, Qt::SmoothTransformation);
            qImage = nqImage;
        }


        uchar *bits = qImage.bits();
        int radius = std::min(qImage.size().width(), qImage.size().height())/2; radius = radius*radius;
        int center_x = qImage.size().width() / 2;
        int center_y = qImage.size().height() / 2;
        int depth = qImage.depth() / 8;

        // save two representation of the picture: a square for the post, and a disk for the user list
        if(!photo.isEmpty()) {
            qImage.save(name + ".square.png", "PNG");
        }


        for(int i = 0 ; i < qImage.size().width() ; ++i) {
            for(int j = 0 ; j < qImage.size().height() ; ++j) {
                int dstCenter = (center_x - i)*(center_x - i) + (center_y - j)*(center_y - j);
                if(dstCenter > radius) {
                    for(int c = 0 ; c < depth ; ++c) {
                        bits[(j*qImage.size().width()+i)*depth+c] = 255*(c != 3);
                    }
                }
            }
        }

        qImage.convertToFormat(QImage::Format_ARGB4444_Premultiplied).save(name, "PNG");


    }

    mutex.lockForWrite();
    sendContact(bareJid);
    mutex.unlock();

}

void XMPP::sendMessageTo(const QString &to, const QString &message) {

    sendPacket(QXmppMessage("", to, message));
}






// =============================================================================
// Remote control API

void XMPP::waitRemote() {
    qDebug() << "Wait remote.";
    m_TcpThreadBind->run();
}

void XMPP::newConnection() {
    m_Socket = boost::shared_ptr<QTcpSocket>(m_Server->nextPendingConnection());

    if (m_Socket->state() == QTcpSocket::ConnectedState) {
        qDebug() << "Hg10: connection established with UI";
    }

    // Make connections for reveiving disconnect and read ready signals for the
    // new connection socket
    bool ok = connect(m_Socket.get(), SIGNAL(disconnected()), this, SLOT(remoteClientDisconect()));
    Q_ASSERT(ok);
    ok = connect(m_Socket.get(), SIGNAL(readyRead()), this, SLOT(readyRead()));
    Q_ASSERT(ok);
    Q_UNUSED(ok);
}


void XMPP::readyRead() {

    QByteArray code_str = m_Socket->read(sizeof(int));
    int code = *reinterpret_cast<int*>(code_str.data());

    qDebug() << "HEADLESS -- MESSAGE: " << code;
    switch(code) {

        case XMPPServiceMessages::LOGIN: {
            qDebug() << "HEADLESS -- LOGIN REQUEST";
                QByteArray code_str = m_Socket->read(sizeof(int));
                int size = *reinterpret_cast<int*>(code_str.data());
                m_User = QString(m_Socket->read(size));

                code_str = m_Socket->read(sizeof(int));
                size = *reinterpret_cast<int*>(code_str.data());
                QString password(m_Socket->read(size));

                if(!m_Connected) {
                    m_LastError = -1;
                    connectToServer(m_User, password);
                } else
                    logConnection();
            }
            break;

        case XMPPServiceMessages::ADVANCED_LOGIN: {
            qDebug() << "HEADLESS -- ADVANCED LOGIN REQUEST";

            QByteArray code_str = m_Socket->read(sizeof(int));
            int size = *reinterpret_cast<int*>(code_str.data());
            QString host(m_Socket->read(size));


            code_str = m_Socket->read(sizeof(int));
            size = *reinterpret_cast<int*>(code_str.data());
            QString domain(m_Socket->read(size));

            code_str = m_Socket->read(sizeof(int));
            int port = *reinterpret_cast<int*>(code_str.data());

            code_str = m_Socket->read(sizeof(int));
            size = *reinterpret_cast<int*>(code_str.data());
            m_User = QString(m_Socket->read(size));

            code_str = m_Socket->read(sizeof(int));
            size = *reinterpret_cast<int*>(code_str.data());
            QString password(m_Socket->read(size));

            code_str = m_Socket->read(sizeof(int));
            int encryption = *reinterpret_cast<int*>(code_str.data());

            if(!m_Connected) {
                 m_LastError = -1;
                 QXmppConfiguration configuration;
                 configuration.setHost(host);
                 configuration.setDomain(domain);
                 configuration.setPort(port);
                 configuration.setUser(m_User);
                 configuration.setPassword(password);
                 configuration.setAutoReconnectionEnabled(true);

                 switch(encryption) {
                     case 0:
                         configuration.setStreamSecurityMode(QXmppConfiguration::TLSEnabled);
                         break;
                     case 1:
                         configuration.setStreamSecurityMode(QXmppConfiguration::TLSDisabled);
                         break;
                     case 2:
                         configuration.setStreamSecurityMode(QXmppConfiguration::TLSRequired);
                         break;
                 }

                 connectToServer(configuration);
            } else
                logConnection();
        }
        break;

        case XMPPServiceMessages::OAUTH2_LOGIN: {
            qDebug() << "HEADLESS -- OAUTH2_LOGIN REQUEST";
            QByteArray code_str = m_Socket->read(sizeof(int));
            int size = *reinterpret_cast<int*>(code_str.data());
            QString user(m_Socket->read(size));

            if(!m_Connected) {
                oauth2Login(user);
            } else
                logConnection();

        }
        break;

        case XMPPServiceMessages::REQUEST_CONNECTION_STATUS: {
            if(m_Connected) {
                logConnection();
            } else {
                logFailedConnecting();
            }
        }
            break;

        case XMPPServiceMessages::DISCONNECT:
            qDebug() << "HEADLESS -- DISCONNECT REQUEST";
            if(m_Connected) {
                disconnectFromServer();
                m_Connected = false;
                QSettings settings("Amonchakai", "Hg10");
                settings.setValue("access_token", "");
            }
            break;

        case XMPPServiceMessages::REQUEST_CONTACT_LIST:
            qDebug() << "HEADLESS -- CONTACT LIST REQUEST";
            mutex.lockForRead();
            if(m_Connected) {
                qDebug() << "roster...";
                mutex.unlock();
                rosterReceived();
            } else {
                qDebug() << "Schedule request...";
                m_SendContactWhenAvailable = true;
                mutex.unlock();
            }
            break;

        case XMPPServiceMessages::REQUEST_CONTACT_LIST_PRESENCE: {

            sendContactsPersence();
        }
            break;


        case XMPPServiceMessages::SEND_MESSAGE: {
            qDebug() << "HEADLESS -- Send message";

            QByteArray code_str = m_Socket->read(sizeof(int));
            int size = *reinterpret_cast<int*>(code_str.data());
            QString to = QString(m_Socket->read(size));

            code_str = m_Socket->read(sizeof(int));
            size = *reinterpret_cast<int*>(code_str.data());
            QString message = QString(m_Socket->read(size));

            sendPacket(QXmppMessage("", to, message));

        }
            break;

        case XMPPServiceMessages::REFRESH_SETTINGS:
            updateSettings();
            break;


        case XMPPServiceMessages::CREATE_ROOM: {
            QByteArray code_str = m_Socket->read(sizeof(int));
            int size = *reinterpret_cast<int*>(code_str.data());
            QString roomName = QString(m_Socket->read(size));

            createRoom(roomName);
        }
            break;


        case XMPPServiceMessages::ADD_PARTICIPANT: {
             QByteArray code_str = m_Socket->read(sizeof(int));
             int size = *reinterpret_cast<int*>(code_str.data());
             QString participantId = QString(m_Socket->read(size));
        }
            break;


    }

}

void XMPP::remoteClientDisconect() {
    disconnect(m_Socket.get(), SIGNAL(disconnected()), this, SLOT(remoteClientDisconect()));
    disconnect(m_Socket.get(), SIGNAL(readyRead()), this, SLOT(readyRead()));
    m_Socket->close();

    waitRemote();
}

void XMPP::sendContact(const QString &contact) {
    qDebug() << "SENDING Contact list";

    if(contact.isEmpty())
        return;

    if (m_Socket && m_Socket->state() == QTcpSocket::ConnectedState) {

        int code = XMPPServiceMessages::REPLY_CONTACT_LIST;
        m_Socket->write(reinterpret_cast<char *>(&code), sizeof(int));

        code = contact.length();
        m_Socket->write(reinterpret_cast<char *>(&code), sizeof(int));
        m_Socket->write(contact.toAscii());

        int status = -1;
        QMap<QString, int>::iterator it = m_ContactList.find(contact);
        if(it != m_ContactList.end())
            status = it.value();

        m_Socket->write(reinterpret_cast<char *>(&status), sizeof(int));

        m_Socket->flush();
    }

}







// receive message
void XMPP::logReceivedMessage(const QString &from, const QString &to, const QString &message) {

    if(message.isEmpty())
        return;

qDebug() << "Log... Start";

    QString fromC = from;
    int id = fromC.indexOf("/");
    if(id != -1)
        fromC = fromC.mid(0,id);

    // message sent from another computer...
    if(from.toLower() == m_User.toLower()) {
        return;
    }

    // --------------------------------------------------------------------------------
    // history file

    TimeEvent e;
    e.m_Who = fromC;
    e.m_What = message;
    e.m_Read = false;
    e.m_When = QDateTime::currentDateTime().toMSecsSinceEpoch();

    QString directory = QDir::homePath() + QLatin1String("/ApplicationData/History");
    if (!QFile::exists(directory)) {
        QDir dir;
        dir.mkpath(directory);
    }

    QFile file(directory + "/" + fromC);

    if (file.open(QIODevice::Append)) {
        QDataStream stream(&file);
        stream << e;

        file.close();
    } else {
        qDebug() << "Cannot write history";
    }
    qDebug() << "write... Start";
    // --------------------------------------------------------------------------------
    // preview file


    QFile file2(directory + "/" + fromC + ".preview");

    if (file2.open(QIODevice::WriteOnly)) {
    QDataStream stream(&file2);
        stream << e;

        file2.close();
    } else {
        //qDebug() << "Cannot write preview";
    }
    qDebug() << "preview... Start";

}













// ===============================================================================================
// Handle extensions
// - Multi user chat : MUC
// - File transfert
// Both appear to be not supported by Google, nor Facebook




void XMPP::initTransfertManager() {
    if(m_TransferManager != NULL)
        return;

    m_TransferManager = boost::shared_ptr<QXmppTransferManager>(new QXmppTransferManager());
    m_TransferManager->setProxy("proxy.eu.jabber.org");
    addExtension(m_TransferManager.get());

    bool check = connect(m_TransferManager.get(), SIGNAL(fileReceived(QXmppTransferJob*)), this, SLOT(fileReceived(QXmppTransferJob*)));
    Q_ASSERT(check);
}



// -------------------------------------------------------------
// file transfer handling

void XMPP::sendData(const QString &file, const QString &to) {
    qDebug() << "Send file request... " << file << to;
    QXmppTransferJob *job = m_TransferManager->sendFile(to, file, "file sent from Hg10");

    bool check = connect(job, SIGNAL(error(QXmppTransferJob::Error)),
                    this, SLOT(transferError(QXmppTransferJob::Error)));
    Q_ASSERT(check);

    check = connect(job, SIGNAL(finished()),
                    this, SLOT(transferFinished()));
    Q_ASSERT(check);

    check = connect(job, SIGNAL(progress(qint64,qint64)),
                    this, SLOT(transferInProgress(qint64,qint64)));
    Q_ASSERT(check);

}


void XMPP::fileReceived(QXmppTransferJob* ) {

}

void XMPP::transferError(QXmppTransferJob::Error error) {
    qDebug() << "Transmission failed:" << error;
}

void XMPP::transferFinished() {
    qDebug() << "Transmission finished";
}

void XMPP::transferInProgress(qint64 done,qint64 total) {
    qDebug() << "Transmission progress:" << done << "/" << total;
}




void XMPP::initGroupChat() {
    if(m_MucManager != NULL)
        return;

    m_MucManager = boost::shared_ptr<QXmppMucManager>(new QXmppMucManager);
    this->addExtension(m_MucManager.get());

    // groupchat.google.com
    bool check = connect(m_MucManager.get(), SIGNAL(invitationReceived(const QString &, const QString &, const QString &)), this, SLOT(invitationReceived(const QString &, const QString &, const QString &)));
    Q_ASSERT(check);
    Q_UNUSED(check);

}





// -------------------------------------------------------------
// group chat handling
// >> for google hangout
// http://stackoverflow.com/questions/14903678/does-google-talk-support-xmpp-multi-user-chat
void XMPP::invitationReceived(const QString & roomJid, const QString & inviter, const QString & reason) {

    qDebug() << roomJid << inviter << reason;

}




void XMPP::roomError(const QXmppStanza::Error &error) {
    qDebug() << "something went wrong... " << error.code();
}

void XMPP::roomJoined() {
    qDebug() << "Room joined...";

    mutex.lockForWrite();
    m_MucRoom->sendMessage("Hoy hoy hoy");
    mutex.unlock();

}

void XMPP::createRoom(const QString &room) {
    if(!m_MucManager)
        initGroupChat();

    GroupChat chatRoom;

    QString directory = QDir::homePath() + QLatin1String("/ApplicationData/Rooms");
    QFile file(directory + "/" + room + ".room");

    if (file.open(QIODevice::ReadOnly)) {
        QDataStream stream(&file);
        stream >> chatRoom;

        file.close();
    } else {
        return;
    }

    qDebug() << "room: " << chatRoom.m_RoomID;
    m_MucRoom = boost::shared_ptr<QXmppMucRoom>(m_MucManager->addRoom(chatRoom.m_RoomID));
    m_MucRoom->setNickName(m_User);

    for(int i = 0 ; i < chatRoom.m_Participants.size() ; ++i) {
        qDebug() << "invite: " << chatRoom.m_Participants.at(i);
        qDebug() << "sent? : " << m_MucRoom->sendInvitation(chatRoom.m_Participants.at(i), "want to talk?");
    }

    m_MucRoom->join();

    qDebug() << "Create room...";
    bool check = connect(m_MucRoom.get(), SIGNAL(error(const QXmppStanza::Error &)), this, SLOT(roomError(const QXmppStanza::Error &)));
    Q_ASSERT(check);

    check = connect(m_MucRoom.get(), SIGNAL(joined()), this, SLOT(roomJoined()));
    Q_ASSERT(check);

}


