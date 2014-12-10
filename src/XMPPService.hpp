/*
 * XMPPController.hpp
 *
 *  Created on: 12 oct. 2014
 *      Author: pierre
 */

#ifndef XMPPSERVICE_HPP_
#define XMPPSERVICE_HPP_

#include "client/QXmppTransferManager.h"
#include "client/QXmppClient.h"
#include "base/QXmppLogger.h"
#include <boost/shared_ptr.hpp>

class QXmppVCardIq;
class QTcpServer;
class QTcpSocket;
class TcpThreadBind;
class QXmppMucManager;
class QXmppMucRoom;
class GoogleConnectController;

class XMPP : public QXmppClient {
    Q_OBJECT;

public:
    XMPP(QObject *parent = 0);
    virtual ~XMPP() {};

    void                           set(XMPP* c)                     {m_This = c;};
    static XMPP*                   get();


private:

    // -------------------------------------------------------------
    // data for XMPP services

    static XMPP                             *m_This;
    bool                                     m_Connected;
    char                                     m_LastError;
    boost::shared_ptr<QXmppTransferManager>  m_TransferManager;
    boost::shared_ptr<QXmppMucManager>       m_MucManager;
    boost::shared_ptr<QXmppMucRoom>          m_MucRoom;
    bool                                     m_SendContactWhenAvailable;
    QString                                  m_User;
    QMap<QString, int>                       m_ContactList;

    boost::shared_ptr<GoogleConnectController> m_GoogleConnect;

    // -------------------------------------------------------------
    // data for remote control from UI application
    boost::shared_ptr<TcpThreadBind>        m_TcpThreadBind;
    int                                     m_Port;
    boost::shared_ptr<QTcpServer>           m_Server;
    boost::shared_ptr<QTcpSocket>           m_Socket;

    bool                      m_NotificationEnabled;
    bool                      m_Restarting;
    bool                      m_VcardManagerConnected;



    void updateSettings     ();
    void oauth2Login        (const QString &user);


    void createRoom         (const QString &room);

public:


public Q_SLOTS:

    void oauth2Restart      ();
    void oauthDisconnected  ();
    void askNewToken        ();
    void readyRestart       (const QString &token);


    void messageReceived    (const QXmppMessage&);
    void sendMessageTo      (const QString &to, const QString &message);
    void presenceChanged    (const QString& bareJid, const QString& resource);

    void rosterReceived     ();
    void vCardReceived      (const QXmppVCardIq&);


    // -------------------------------------------------------------
    // file transfer handling

    void sendData           (const QString &file, const QString &to);
    void fileReceived       (QXmppTransferJob* );
    void transferError      (QXmppTransferJob::Error error);
    void transferFinished   ();
    void transferInProgress (qint64 done,qint64 total);

    // -------------------------------------------------------------
    // Side API
    void initTransfertManager   ();
    void initGroupChat          ();
    void invitationReceived     (const QString &     roomJid,
                                 const QString &     inviter,
                                 const QString &     reason);
    void roomError              (const QXmppStanza::Error &error);
    void roomJoined             ();


    void logConnection          ();
    void logFailedConnecting    ();
    void logConnectionError     (QXmppClient::Error error);


    // -------------------------------------------------------------
    // Remote control functions

    void waitRemote             ();
    void newConnection          ();
    void readyRead              ();
    void remoteClientDisconect  ();



    void sendContact            (const QString &contact);
    void sendContactsPersence   ();
    void logReceivedMessage     (const QString &from, const QString &to, const QString &message);

Q_SIGNALS:

    void offline            (bool status);
    void presenceUpdated    (const QString &who, int status);
    void quitApp            ();

};


#endif /* XMPPSERVICE_HPP_ */
