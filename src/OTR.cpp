/*
 * OTR.cpp
 *
 *  Created on: 28 juin 2015
 *      Author: pierre
 */

#include "OTR.h"
#include "XMPPService.hpp"

extern "C" {
    #include <proto.h>
    #include <privkey.h>
    #include <message.h>
    #include <b64.h>
}

#include <QDir>
#include <QTextCodec>
#include <QSet>

// =====================================================================================================
// some global variable


bool secure = false;
OtrlUserState us;
QSet<QString> g_encrypted;


// =====================================================================================================
// OTR internal functions

static OtrlPolicy myotr_policy(void *opdata, ConnContext *context)
{
    return OTRL_POLICY_ALLOW_V2;
     //   | OTRL_POLICY_REQUIRE_ENCRYPTION;
}


static void myotr_inject_message(void *opdata,
                                 const char *accountname, const char *protocol, const char *recipient,
                                 const char *message)
{
    // my pipe is XMPP, so I just send everything via XMPP messages (this include your messages, but also the core messages from the OTR protocol)
    // the opdata is a pointer to some data you would want to provide, for example a socket or so.
    // here, I have my singleton so I don't need anything

    XMPP::get()->sendXMPPMessageTo(recipient, message);
}


static void myotr_gone_secure(void *opdata, ConnContext *context)
{
    // when this is fired, from now on, everything is encrypted!
    qDebug() << "myotr_gone_secure()" ;
    secure = 1;
    XMPP::get()->goneSecure(context->username);

    if(g_encrypted.find(context->username) == g_encrypted.end())
        g_encrypted.insert(context->username);
}

static void myotr_gone_insecure(void *opdata, ConnContext *context)
{
    qDebug() << "myotr_gone_insecure() WARNING!" ;
    secure = 0;
    XMPP::get()->goneUnsecure(context->username);

    if(g_encrypted.find(context->username) != g_encrypted.end())
        g_encrypted.remove(context->username);
}


static QString fingerprint_hash_to_human( unsigned char* hash )
{
    char human[45];
    bzero( human, 45 );
    otrl_privkey_hash_to_human( human, hash );
    QString ret(human);
    return ret;
}

static QString fingerprint_hash_to_human( char* hash )
{
    return fingerprint_hash_to_human( reinterpret_cast<unsigned char*>(hash) );
}



static void myotr_new_fingerprint( void *opdata, OtrlUserState us,
                                   const char *accountname, const char *protocol,
                                   const char *username, unsigned char fingerprint[20])
{
    /*
    qDebug() << "myotr_new_fingerprint(top)" ;

    char our_fingerprint[45];
    if( otrl_privkey_fingerprint( us, our_fingerprint, accountname, protocol) )
    {
        qDebug() << "myotr_new_fingerprint() our   human fingerprint:" <<  our_fingerprint ;
    }*/

    XMPP::get()->fingerprintReceived(username, fingerprint_hash_to_human(fingerprint));

}



static void myotr_write_fingerprint(void *opdata) {
    qDebug() << "write fingerprints";
}

int max_message_size(void *opdata, ConnContext *context) {
#ifdef FRAG40
  return 100;
#else
  return 0;
#endif
}

void handle_msg_event(void *opdata, OtrlMessageEvent msg_event, ConnContext *context, const char *message, gcry_error_t err) {
  char* msg = "";

  switch(msg_event) {
    case OTRL_MSGEVENT_NONE:
      msg = "OTRL_MSGEVENT_NONE";
      break;
    case OTRL_MSGEVENT_ENCRYPTION_REQUIRED:
      msg = "OTRL_MSGEVENT_ENCRYPTION_REQUIRED";
      break;
    case OTRL_MSGEVENT_ENCRYPTION_ERROR:
      msg = "OTRL_MSGEVENT_ENCRYPTION_ERROR";
      break;
    case OTRL_MSGEVENT_CONNECTION_ENDED:
      msg = "OTRL_MSGEVENT_CONNECTION_ENDED";
      if(g_encrypted.find(context->username) != g_encrypted.end())
          g_encrypted.remove(context->username);

      break;
    case OTRL_MSGEVENT_SETUP_ERROR:
      msg = "OTRL_MSGEVENT_SETUP_ERROR";
      break;
    case OTRL_MSGEVENT_MSG_REFLECTED:
      msg = "OTRL_MSGEVENT_MSG_REFLECTED";
      break;
    case OTRL_MSGEVENT_MSG_RESENT:
      msg = "OTRL_MSGEVENT_MSG_RESENT";
      break;
    case OTRL_MSGEVENT_RCVDMSG_NOT_IN_PRIVATE:
      msg = "OTRL_MSGEVENT_RCVDMSG_NOT_IN_PRIVATE";
      break;
    case OTRL_MSGEVENT_RCVDMSG_UNREADABLE:
      msg = "OTRL_MSGEVENT_RCVDMSG_UNREADABLE";
      break;
    case OTRL_MSGEVENT_RCVDMSG_MALFORMED:
      msg = "OTRL_MSGEVENT_RCVDMSG_MALFORMED";
      break;
    case OTRL_MSGEVENT_LOG_HEARTBEAT_RCVD:
      msg = "OTRL_MSGEVENT_LOG_HEARTBEAT_RCVD";
      break;
    case OTRL_MSGEVENT_LOG_HEARTBEAT_SENT:
      msg = "OTRL_MSGEVENT_LOG_HEARTBEAT_SENT";
      break;
    case OTRL_MSGEVENT_RCVDMSG_GENERAL_ERR:
      msg = "OTRL_MSGEVENT_RCVDMSG_GENERAL_ERR";
      break;
    case OTRL_MSGEVENT_RCVDMSG_UNENCRYPTED:
      msg = "OTRL_MSGEVENT_RCVDMSG_UNENCRYPTED";
      if(g_encrypted.find(context->username) != g_encrypted.end())
          g_encrypted.remove(context->username);
      disconnectOTR(context->accountname, context->username, context->protocol);
      XMPP::get()->goneUnsecure(context->username);
      break;
    case OTRL_MSGEVENT_RCVDMSG_UNRECOGNIZED:
      msg = "OTRL_MSGEVENT_RCVDMSG_UNRECOGNIZED";
      break;
    case OTRL_MSGEVENT_RCVDMSG_FOR_OTHER_INSTANCE:
      msg = "OTRL_MSGEVENT_RCVDMSG_FOR_OTHER_INSTANCE";
      break;
  }

  qDebug() << msg;

}


// function table for OTR: map our custom functions
static OtrlMessageAppOps ui_ops = {
    myotr_policy,
    NULL,
    NULL,
    myotr_inject_message,
    NULL,
    myotr_new_fingerprint,
    myotr_write_fingerprint,
    myotr_gone_secure,
    myotr_gone_insecure,
    NULL,
    max_message_size,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    handle_msg_event,
    NULL,
    NULL,
    NULL
};



// =====================================================================================================
// use library


void initOTR() {
    //OTRL_INIT;

    us = otrl_userstate_create();

    QString keyDir = QDir::homePath() + QLatin1String("/keys");
    otrl_privkey_read_fingerprints( us, (keyDir+"/fingerprint.txt").toAscii(), 0, 0 );
}


void ownFingerprint(const QString& accountname, const QString& protocol) {
    char our_fingerprint[45];
    if( otrl_privkey_fingerprint( us, our_fingerprint, accountname.toAscii(), protocol.toAscii()) )
    {
        XMPP::get()->sendOurFingerprint(our_fingerprint) ;
    }
}


void disconnectOTR(const QString& ourAccount, const QString& account, const QString& protocol) {
     otrl_message_disconnect(us, &ui_ops, NULL, ourAccount.toAscii(), protocol.toAscii(), account.toAscii(), OTRL_INSTAG_BEST);
}

void loadKeyIfExist (const QString& filename) {
    gcry_error_t et;
    et = otrl_privkey_read( us, filename.toAscii());

    if(gcry_err_code(et) != GPG_ERR_NO_ERROR)
        qDebug() << "need to create a key";
}

void setupKeys(const QString& filename, const QString &accountName, const QString& protocol) {


    gcry_error_t et;
    et = otrl_privkey_read( us, filename.toAscii());


    if(gcry_err_code(et) != GPG_ERR_NO_ERROR) {
        qDebug() << "need to create a key";
        if(gcry_err_code(et) != GPG_ERR_NO_ERROR) {
            et = otrl_privkey_generate( us, filename.toAscii(), accountName.toAscii(), protocol.toAscii() );
        } else {
            qWarning() << "Oups, something went wrong while creating the key!";
        }
    }
}

bool encryptionStatus(const QString& account) {
    return g_encrypted.find(account) != g_encrypted.end();
}

void startOTRSession(const QString& ourAccount, const QString& account) {
    send_message (ourAccount, account, "xmpp", "?OTRv23?");
}



void message_received(const QString& ourAccount, const QString& account, const QString& protocol, const QString& message) {
    uint32_t ignore = 0;
    char *new_message = NULL;
    OtrlTLV *tlvs = NULL;


    ignore = otrl_message_receiving(us, &ui_ops, NULL, ourAccount.toAscii(), protocol.toAscii(), account.toAscii(), message.toAscii(), &new_message, &tlvs, NULL, NULL, NULL);

    // if ignore == 1, then it is a core message from OTR. We don't want to display that.
    if(ignore == 0) {

        if (new_message) {
            QString ourm = QString(QTextCodec::codecForName("UTF-8")->toUnicode(new_message));
            otrl_message_free(new_message);



            // encrypted message
            XMPP::get()->fowardMessageToView(account, ourAccount, ourm);
        } else {

            // non encrypted message
            XMPP::get()->fowardMessageToView(account, ourAccount, message);
        }
    }

    if(tlvs) {
        qDebug() << "there are side info!";
        otrl_tlv_free(tlvs);
    }
}


bool send_message (const QString& ourAccount, const QString& account, const QString& protocol, const QString& message) {
    char *new_message = NULL;
    gcry_error_t err;

    QByteArray sentMess = message.toUtf8();
    err = otrl_message_sending(us, &ui_ops, NULL, ourAccount.toAscii(), protocol.toAscii(), account.toAscii(), OTRL_INSTAG_BEST, sentMess.data(), NULL, &new_message,
            OTRL_FRAGMENT_SEND_SKIP, NULL, NULL, NULL);

    if (new_message) {
        QString ourm(new_message);
        otrl_message_free(new_message);

        return XMPP::get()->sendXMPPMessageTo(account, ourm);
    } else {
        if(err == 0) {
            if(g_encrypted.find(account) != g_encrypted.end()) {
                g_encrypted.remove(account);
                XMPP::get()->sendDenied(account, message);
            } else {
                return XMPP::get()->sendXMPPMessageTo(account, message);
            }

        }
    }

    if (err) {
        qDebug() << "plouf! encryption failed";
        return false;
    }

    return false;
}
