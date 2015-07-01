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
#include <QNetworkProxy>

#include "Hub/HubIntegration.hpp"
#include "GoogleConnectController.hpp"
#include "PrivateAPIKeys.h"
#include "HeadlessApplication.hpp"
#include "OTR.h"


XMPP* XMPP::m_This = NULL;
QReadWriteLock  mutex;
QReadWriteLock  mutexLoadLocal;

// ===============================================================================================
// Utility class: start the TCP server which wait for UI in another thread



TcpThreadBind::TcpThreadBind(QObject *parent) : QThread(parent), m_Port(0) {

}

void TcpThreadBind::run() {
    if(m_Server != NULL) {
        if(!m_Server->isListening())
            m_Server->listen(QHostAddress::LocalHost, m_Port);
    }
}

void TcpThreadBind::process() {
    run();
}

// ===============================================================================================
// The Core XMPP Service : a QMPPClient


XMPP::XMPP(QObject *parent) : QXmppClient(parent),
        m_Hub(NULL),
        m_App(NULL),
        m_Connected(false),
        m_LastError(0),
        m_SendContactWhenAvailable(false),
        m_ConnectionType(OTHER),

        m_Port(27015),
        m_NotificationEnabled(true),
        m_Restarting(false),
        m_VcardManagerConnected(false) {


    m_PauseService = false;

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



    initOTR();


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
        // connection using user ID/PASSWORD (XMPP or GOOGLE LEGACY)
        simpleConnectRestart();

    }

    waitRemote();

}


void XMPP::waitForInternet() {
    m_PauseService = true;
    qDebug() << "INTERNET access has been lost. Disconnect & wait it return.";

    oauthDisconnected();
}


void XMPP::internetIsBack() {
    m_PauseService = false;
    qDebug() << "INTERNET access is back.";

    if(m_ConnectionType == GOOGLE) {
        oauth2Restart();
    } else {
        simpleConnectRestart();
    }

}




void XMPP::simpleConnectRestart() {

    if(m_PauseService) return;

    // ---------------------------------------------------------------------
    // connection using user ID/PASSWORD (XMPP or GOOGLE LEGACY)

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
            m_ConnectionType = OTHER;
            if(host.isEmpty())
                connectToServer(m_User, password);
            else {
                QXmppConfiguration configuration;
                configuration.setHost(host);
                configuration.setDomain(domain);
                configuration.setPort(port);
                configuration.setUser(m_User);
                configuration.setPassword(password);
//                    configuration.setAutoReconnectionEnabled(true);

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
    if(m_PauseService) return;

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
    m_ConnectionType = GOOGLE;

}

// case 3:
// Receive order from view to start a connection
void XMPP::oauth2Login(const QString &user) {

    QSettings settings("Amonchakai", "Hg10");

    m_User = user;

    qDebug() << "oauth2Login: " << m_User << " length(access_token): " << settings.value("access_token").value<QString>().length();

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
        m_ConnectionType = GOOGLE;

        return;
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


void XMPP::fowardMessageToView(const QString &from, const QString &to, const QString &message) {

    if (m_Socket && m_Socket->state() == QTcpSocket::ConnectedState) {
        mutex.lockForWrite();

        int code = XMPPServiceMessages::MESSAGE;
        m_Socket->write(reinterpret_cast<char *>(&code), sizeof(int));

        int length = from.length();
        m_Socket->write(reinterpret_cast<char *>(&length), sizeof(int));
        m_Socket->write(from.toAscii(), length);

        length = to.length();
        m_Socket->write(reinterpret_cast<char *>(&length), sizeof(int));
        m_Socket->write(to.toAscii(), length);

        QByteArray sentMess = message.toUtf8();
        length = sentMess.size();
        m_Socket->write(reinterpret_cast<char *>(&length), sizeof(int));
        m_Socket->write(sentMess.data(), length);
        m_Socket->flush();

        mutex.unlock();
    } else {

        logReceivedMessage(from, to, message);

        if(m_NotificationEnabled) {
            bb::platform::Notification notif;
            notif.notify();
            emit updateHubAccount();
        }
    }
}


void XMPP::messageReceived(const QXmppMessage& message) {

    // cleanup the user name, indeed the name could be amonchakai@someting.de/mbpr
    // but all the rest of the code deals with amonchakai@someting.de
    QString from = message.from();
    int i = from.lastIndexOf('/');
    if(i != -1) {
        from = from.mid(0, i);
    }

    if (m_Socket && m_Socket->state() == QTcpSocket::ConnectedState) {

        if(message.body().isEmpty()) {
            mutex.lockForWrite();
            int code = XMPPServiceMessages::STATUS_UPDATE;
            m_Socket->write(reinterpret_cast<char *>(&code), sizeof(int));

            int length = message.from().length();
            m_Socket->write(reinterpret_cast<char *>(&length), sizeof(int));
            m_Socket->write(from.toAscii(), length);

            code = message.state();
            m_Socket->write(reinterpret_cast<char *>(&code), sizeof(int));
            m_Socket->flush();
            mutex.unlock();
        } else {
            // Message need to be decrypted (if needed)
            qDebug() << m_User << from << message.body();
            message_received(m_User, from, "xmpp", message.body());

        }
    } else {

        if(!message.body().isEmpty())
            message_received(m_User, from, "xmpp", message.body());
    }
}


void XMPP::presenceChanged(const QString& bareJid, const QString& resource) {
    mutex.lockForWrite();

    int status = rosterManager().getPresence(bareJid, resource).availableStatusType();
    m_ContactList[bareJid] = status;

    // this is a backup solution: apparently I cannot receive the vcard from non google contact, however, I can see their presence.
    // then, I check if I have a corresponding vcard for the contact for which I receive presence information.
    // if not available, create an empty vcard whith just ID info and send the contact info.
    QString vCardsDir = QDir::homePath() + QLatin1String("/vCards");
    if(!QFile::exists(vCardsDir + "/" + bareJid + ".xml")) {
        writeEmptyCard(bareJid);
        sendContact(bareJid);
    }

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
            vCardManager().requestVCard(list.at(i));
        } else {

            // -------------------------------------------------------------
            // get vCard from file

            QString vCardsDir = QDir::homePath() + QLatin1String("/vCards");
            QFile file(vCardsDir + "/" + list.at(i) + ".xml");

            QDomDocument doc("vCard");
            if (!file.open(QIODevice::ReadOnly))
                return;
            if (!doc.setContent(&file)) {
            }
            file.close();

            QXmppVCardIq vCard;
            vCard.parse(doc.documentElement());

            if(vCard.fullName().isEmpty() || vCard.fullName() == list.at(i)) {
                vCardManager().requestVCard(list.at(i));
            } else {
                // if card already exists, then no need to request it.
                mutex.lockForWrite();
                sendContact(list.at(i));
                mutex.unlock();
            }
        }
    }

    mutexLoadLocal.unlock();
    emit offline(false);

    // --------------------------------------------------------------
    // load OTR Keys, if exists

    QString keyDir = QDir::homePath() + QLatin1String("/keys");
    QDir dir;
    if(!dir.exists(keyDir))
        dir.mkdir(keyDir);
    else
        loadKeyIfExist(keyDir+"/keys.txt");


    // --------------------------------------------------------------
    // connection is successful, send cached messages.

    bool over = false;
    while(!over) {
        over = m_MessageBuffer.empty();

        if(!over) {
            if(send_message(m_User, m_MessageBuffer.first().first, "xmpp", m_MessageBuffer.first().second)) {
                m_MessageBuffer.pop_front();
            }
        }
    }

}

void XMPP::writeEmptyCard(const QString &bareJid) {

    QString vCardsDir = QDir::homePath() + QLatin1String("/vCards");

    QXmppVCardIq vCard;
    vCard.setFrom(bareJid);
    vCard.setFullName(bareJid);

    QFile file(vCardsDir + "/" + bareJid + ".xml");

    if(file.open(QIODevice::ReadWrite)) {
        QXmlStreamWriter stream(&file);
        vCard.toXml(&stream);
        file.close();
    }
}

void XMPP::vCardReceived(const QXmppVCardIq& vCard) {
    QString bareJid = vCard.from();
    QString vCardsDir = QDir::homePath() + QLatin1String("/vCards");

    if(bareJid.isEmpty())
        bareJid = m_User;

    QDir dir;
    if(!dir.exists(vCardsDir))
        dir.mkdir(vCardsDir);


    mutex.lockForWrite(); // also secure writing the vcard

    QFile file(vCardsDir + "/" + bareJid + ".xml");
    if(file.open(QIODevice::WriteOnly))
    {
        QXmlStreamWriter stream(&file);
        vCard.toXml(&stream);
        file.close();
    }


    sendContact(bareJid);
    mutex.unlock();

}


void XMPP::goneSecure(const QString& with) {
    mutex.lockForWrite();

    if (m_Socket && m_Socket->state() == QTcpSocket::ConnectedState) {
        int code = XMPPServiceMessages::OTR_GONE_SECURE;
        m_Socket->write(reinterpret_cast<char *>(&code), sizeof(int));

        int length = with.length();
        m_Socket->write(reinterpret_cast<char *>(&length), sizeof(int));
        m_Socket->write(with.toAscii(), length);

        m_Socket->flush();
    }

    mutex.unlock();
}


void XMPP::goneUnsecure(const QString& with) {
    mutex.lockForWrite();

    if (m_Socket && m_Socket->state() == QTcpSocket::ConnectedState) {
        int code = XMPPServiceMessages::OTR_GONE_UNSECURE;
        m_Socket->write(reinterpret_cast<char *>(&code), sizeof(int));

        int length = with.length();
        m_Socket->write(reinterpret_cast<char *>(&length), sizeof(int));
        m_Socket->write(with.toAscii(), length);

        m_Socket->flush();
    }

    mutex.unlock();
}

void XMPP::fingerprintReceived(const QString& from, const QString& fingerprint) {
    mutex.lockForWrite();

    if (m_Socket && m_Socket->state() == QTcpSocket::ConnectedState) {
        int code = XMPPServiceMessages::OTR_FINGERPRINT_RECEIVED;
        m_Socket->write(reinterpret_cast<char *>(&code), sizeof(int));

        int length = from.length();
        m_Socket->write(reinterpret_cast<char *>(&length), sizeof(int));
        m_Socket->write(from.toAscii(), length);

        length = fingerprint.length();
        m_Socket->write(reinterpret_cast<char *>(&length), sizeof(int));
        m_Socket->write(fingerprint.toAscii(), length);

        m_Socket->flush();
    }

    mutex.unlock();
}

void XMPP::sendDenied(const QString& from, const QString& message) {
    mutex.lockForWrite();

    if (m_Socket && m_Socket->state() == QTcpSocket::ConnectedState) {
        int code = XMPPServiceMessages::OTR_WAS_NOT_SECURE;
        m_Socket->write(reinterpret_cast<char *>(&code), sizeof(int));

        int length = from.length();
        m_Socket->write(reinterpret_cast<char *>(&length), sizeof(int));
        m_Socket->write(from.toAscii(), length);

        QByteArray sentMess = message.toUtf8();
        length = sentMess.length();
        m_Socket->write(reinterpret_cast<char *>(&length), sizeof(int));
        m_Socket->write(sentMess.data(), length);

        m_Socket->flush();
    }

    mutex.unlock();
}

void XMPP::sendOTRStatus(const QString& contact) {
    if(encryptionStatus(contact)) {
        goneSecure(contact);
    } else {
        goneUnsecure(contact);
    }
}

void XMPP::sendOurFingerprint(const QString& fingerprint) {
    mutex.lockForWrite();

    if (m_Socket && m_Socket->state() == QTcpSocket::ConnectedState) {
        int code = XMPPServiceMessages::OTR_OWN_FINGERPRINT;
        m_Socket->write(reinterpret_cast<char *>(&code), sizeof(int));

        int length = fingerprint.length();
        m_Socket->write(reinterpret_cast<char *>(&length), sizeof(int));
        m_Socket->write(fingerprint.toAscii(), length);

        m_Socket->flush();
    }

    mutex.unlock();
}


bool XMPP::sendXMPPMessageTo(const QString &to, const QString &message) {

    qDebug() << "send XMPP";

    if(!sendPacket(QXmppMessage("", to, message))) {

        qDebug() << "NOT SENT, CACHE";

        // -------------------------------------------------
        // the message could not be sent, cache & reconnect

        bool message_request_sent = false;
        for(int i = 0 ; i < m_MessageBuffer.size() ; ++i) {
            if(m_MessageBuffer.at(i).first == to && m_MessageBuffer.at(i).second == message) {
                message_request_sent = true;
                break;
            }
        }

        if(!message_request_sent)
            m_MessageBuffer.push_back(QPair<QString, QString>(to, message));

        if(m_ConnectionType == GOOGLE) {
            oauth2Restart();
        } else {
            simpleConnectRestart();
        }

        return false;
    }

    return true;
}






// =============================================================================
// Remote control API

void XMPP::waitRemote() {
    qDebug() << "Wait remote.";
    QTimer::singleShot(1000, m_TcpThreadBind.get(), SLOT(process()));

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
                    m_ConnectionType = OTHER;
                    QSettings settings("Amonchakai", "Hg10");
                    settings.setValue("access_token", "");
                    settings.setValue("User", m_User);
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

            code_str = m_Socket->read(sizeof(int));
            int proxy = *reinterpret_cast<int*>(code_str.data());

            if(proxy == 1)
                detectProxy();


            if(!m_Connected) {
                 m_LastError = -1;
                 QXmppConfiguration configuration;
                 configuration.setHost(host);
                 configuration.setDomain(domain);
                 configuration.setPort(port);
                 configuration.setUser(m_User);
                 configuration.setPassword(password);
//                 configuration.setAutoReconnectionEnabled(true);

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

                 m_ConnectionType = OTHER;
                 QSettings settings("Amonchakai", "Hg10");
                 settings.setValue("access_token", "");
                 settings.setValue("User", m_User);
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
                m_ContactList.clear();
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


            QByteArray code_str = m_Socket->read(sizeof(int));
            int size = *reinterpret_cast<int*>(code_str.data());
            QString to = QString(m_Socket->read(size));

            code_str = m_Socket->read(sizeof(int));
            size = *reinterpret_cast<int*>(code_str.data());
            QString message = QString(QTextCodec::codecForName("UTF-8")->toUnicode(m_Socket->read(size)));

            qDebug() << "HEADLESS -- Send message: " << message;
            send_message(m_User, to, "xmpp", message);

        }
            break;

        case XMPPServiceMessages::REFRESH_SETTINGS:
            updateSettings();
            break;


        case XMPPServiceMessages::SET_STATUS_TEXT: {
            QByteArray code_str = m_Socket->read(sizeof(int));
            int size = *reinterpret_cast<int*>(code_str.data());
            QString text = QString(m_Socket->read(size));

            code_str = m_Socket->read(sizeof(int));
            int presence = *reinterpret_cast<int*>(code_str.data());

            QXmppPresence s = clientPresence();
            s.setPriority(20);
            s.setType(QXmppPresence::Available);
            s.setStatusText(text);
            s.setAvailableStatusType(static_cast<QXmppPresence::AvailableStatusType>(presence));




            {
                // -------------------------------------------------------------
                // get vCard from file
                QString vCardsDir = QDir::homePath() + QLatin1String("/vCards");
                QFile file(vCardsDir + "/" + configuration().jidBare().toLower() + ".xml");

                QDomDocument doc("vCard");
                file.open(QIODevice::ReadOnly);

                if (!doc.setContent(&file)) {
                    //file.close();
                    //return;
                }
                file.close();

                QXmppVCardIq vCard;
                vCard.parse(doc.documentElement());

                if(vCard.photo().isEmpty())
                    s.setVCardUpdateType(QXmppPresence::VCardUpdateNoPhoto);
                else {
                    s.setVCardUpdateType(QXmppPresence::VCardUpdateValidPhoto);
                    qDebug() << "Valid photo!";
                }
                s.setPhotoHash(vCard.photo());
            }

            qDebug() << "SET STATUS : " << static_cast<QXmppPresence::AvailableStatusType>(presence) << configuration().jidBare();

            this->setClientPresence(s);
        }
            break;


        case XMPPServiceMessages::SET_PRESENCE: {
            QByteArray code_str = m_Socket->read(sizeof(int));
            int code = *reinterpret_cast<int*>(code_str.data());

            QXmppPresence s;
            s.setPriority(20);
            s.setAvailableStatusType(static_cast<QXmppPresence::AvailableStatusType>(code));
            this->setClientPresence(s);

        }
            break;

        case XMPPServiceMessages::ADD_CONTACT: {
            QByteArray code_str = m_Socket->read(sizeof(int));
            int size = *reinterpret_cast<int*>(code_str.data());
            QString contactId = QString(m_Socket->read(size));

            QXmppRosterIq nContact;
            nContact.setType(QXmppIq::Set);
            QXmppRosterIq::Item itemAdded;
            itemAdded.setBareJid(contactId);
            itemAdded.setSubscriptionType(QXmppRosterIq::Item::Both);
            nContact.addItem(itemAdded);
            sendPacket(nContact);

        }
            break;



        // --------------------------------------------------------
        // OTR messages

        case XMPPServiceMessages::OTR_REQUEST_START: {
            QByteArray code_str = m_Socket->read(sizeof(int));
            int size = *reinterpret_cast<int*>(code_str.data());
            QString contact = QString(m_Socket->read(size));

            startOTRSession(m_User, contact);

        }
            break;

        case XMPPServiceMessages::OTR_REQUEST_STOP: {
            QByteArray code_str = m_Socket->read(sizeof(int));
            int size = *reinterpret_cast<int*>(code_str.data());
            QString contact = QString(m_Socket->read(size));

            disconnectOTR(m_User, contact, "xmpp");

        }
            break;


        case XMPPServiceMessages::OTR_SETUP_KEYS: {

            // --------------------------------------------------------------
            // connection is successful, setup keys for OTR.

            QString keyDir = QDir::homePath() + QLatin1String("/keys");
            QDir dir;
            if(!dir.exists(keyDir))
                dir.mkdir(keyDir);

            setupKeys(keyDir+"/keys.txt", m_User, "xmpp");
        }
            break;


        case XMPPServiceMessages::OTR_REQUEST_STATUS: {
            QByteArray code_str = m_Socket->read(sizeof(int));
            int size = *reinterpret_cast<int*>(code_str.data());
            QString contact = QString(m_Socket->read(size));

            sendOTRStatus(contact);
        }
            break;

        case XMPPServiceMessages::OTR_GET_OWN_FINGERPRINT: {
            QByteArray code_str = m_Socket->read(sizeof(int));
            int size = *reinterpret_cast<int*>(code_str.data());
            QString contact = QString(m_Socket->read(size));

            ownFingerprint(m_User, "xmpp");
        }
            break;

        // --------------------------------------------------------
        // MUC messages
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


        // --------------------------------------------------------
        // Hub messages

        case XMPPServiceMessages::UPDATE_HUB: {
            emit updateHubAccount();
            break;
        }

        case XMPPServiceMessages::REMOVE_HUB: {
            if(m_Hub)
                m_Hub->removeAccounts();
            break;
        }

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
// handle PROXY

void XMPP::detectProxy() {
    // Set up proxy if we need one
    // Code from httpProxy sample
    netstatus_proxy_details_t details;
    memset(&details, 0, sizeof(details));
    bool proxyWasNotRequired = false;
    if (netstatus_get_proxy_details(&details) != BPS_FAILURE) {
        /* if proxy is required, then set proxy */
        if (details.http_proxy_host == NULL) {
            proxyWasNotRequired = true;
            qDebug() << "No proxy required!";
        } else {
            // Create proxy and set details as available
            m_Proxy = new QNetworkProxy();
            m_Proxy->setType(QNetworkProxy::HttpProxy);
            m_Proxy->setHostName(details.http_proxy_host);
            if (details.http_proxy_port != 0) {
                m_Proxy->setPort(details.http_proxy_port);
            }
            if (details.http_proxy_login_user != NULL) {
                m_Proxy->setUser(details.http_proxy_login_user);
            }
            if (details.http_proxy_login_password != NULL) {
                m_Proxy->setPassword(details.http_proxy_login_password);
            }

            QNetworkProxy::setApplicationProxy(*m_Proxy);
        }
    } else {
        qDebug() << "Error attempting to get proxy details";
    }
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

