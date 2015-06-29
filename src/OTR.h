/*
 * OTR.h
 *
 *  Created on: 28 juin 2015
 *      Author: pierre
 */

#ifndef OTR_H_
#define OTR_H_

#include <QString>

/*
 * Support for OTR using libotr
 *
 * The use of this library is very simple:
 *
 *  - first initialize the library:
 *          - use the macro: OTRL_INIT; somewhere in your code (should be only called once!)
 *          - use otrl_userstate_create to create a structure which will keep all your keys and remember with which participant you can have a secure chat
 *            a otrl_userstate should be created per account: here I only support 1 account, so there is just one
 *
 *  - You also need to load the key, and make one if necessary. The process of making a key can be long, it requires a lot of entropy. Avoid creating too many keys...
 *
 *
 *
 *  Then libotr do all the job of encrypting, exchanging keys, ...
 *
 *  - What you need to do, it to indicate to libotr how to send messages. To do that, there is the structure: OtrlMessageAppOps
 *     which describes a list of function that libotr will need to use.
 *
 *  - You will be interested into defining: otr_inject_message (here called myotr_inject_message) which tells to libotr how to send a message.
 *    In this case, I use OTR on top of XMPP, so I simply call my function to send messages via XMPP to the right person.
 *    All OTR messages are then inside the body of the XMPP messages.
 *
 *
 *  - *** All messages*** you receive should be forwarded to LibOTR, if needed, it will do the job of decrypting messages, sending keys, ...
 *    anything related to the OTR protocol, or do nothing if the message is not encrypted.
 *    Since you specified how to send messages though your pipe, it will do everything by itself
 *
 *
 *  - Similarly, *** All messages *** you send should go though LibOTR, and it will do what is necessary.
 *    the function 'otrl_message_sending', encrypt the messages, and return you the message to be transmitted
 *    (you have to send it yourself)
 *
 *
 *
 *  That is pretty simple to use :-)
 *
 *
 */


void initOTR            ();
void startOTRSession    (const QString& ourAccount, const QString& account);
void loadKeyIfExist     (const QString& filename);
void setupKeys          (const QString& filename, const QString &accountName, const QString& protocol);
void message_received   (const QString& ourAccount, const QString& account, const QString& protocol, const QString& message);
bool send_message       (const QString& ourAccount, const QString& account, const QString& protocol, const QString& message);


#endif /* OTR_H_ */
