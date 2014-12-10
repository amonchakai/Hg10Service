/*
 * GoogleConnectController.cpp
 *
 *  Created on: 19 oct. 2014
 *      Author: pierre
 */

#include "GoogleConnectController.hpp"
#include "Image/HFRNetworkAccessManager.hpp"
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegExp>

#include <QDir>
#include <QFile>
#include <limits>
#include <QReadWriteLock>

#include "PrivateAPIKeys.h"
/*
 * #include "PrivateAPIKeys.h"
 *  define:
 *  - GOOGLE_CIENT_ID
 *  - GOOGLE_CLIENT_SECRET
 *  - GOOGLE_API_KEY
 *
 *  which can be obtained from the google developer console:
 *  https://console.developers.google.com
 *
 */


QReadWriteLock  mutexGoogleConnect;

GoogleConnectController::GoogleConnectController(QObject *parent) : QObject(parent),
    m_Settings(NULL) {

    m_Settings = new QSettings("Amonchakai", "Hg10");
}

void GoogleConnectController::getToken() {

    QNetworkRequest request(QUrl("https://accounts.google.com/o/oauth2/token"));
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/x-www-form-urlencoded");

    QUrl params;
    params.addQueryItem("code",         m_Settings->value("key").value<QString>());
    params.addQueryItem("client_id",    m_Settings->value("Client_ID").value<QString>());
    params.addQueryItem("client_secret", m_Settings->value("ClientSecret").value<QString>());
    params.addQueryItem("redirect_uri", "urn:ietf:wg:oauth:2.0:oob");
    params.addQueryItem("grant_type", "authorization_code");

    QNetworkReply* reply = HFRNetworkAccessManager::get()->post(request, params.encodedQuery());
    bool ok = connect(reply, SIGNAL(finished()), this, SLOT(checkReply()));
    Q_ASSERT(ok);
    Q_UNUSED(ok);
}


void GoogleConnectController::renewToken() {
    QNetworkRequest request(QUrl("https://accounts.google.com/o/oauth2/token"));
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/x-www-form-urlencoded");

    QUrl params;
    params.addQueryItem("client_id",    m_Settings->value("Client_ID").value<QString>());
    params.addQueryItem("client_secret", m_Settings->value("ClientSecret").value<QString>());
    params.addQueryItem("refresh_token", m_Settings->value("refresh_token").value<QString>());
    params.addQueryItem("grant_type", "refresh_token");

    QNetworkReply* reply = HFRNetworkAccessManager::get()->post(request, params.encodedQuery());
    bool ok = connect(reply, SIGNAL(finished()), this, SLOT(checkRefresh()));
    Q_ASSERT(ok);
    Q_UNUSED(ok);
}


void GoogleConnectController::checkReply() {
    QNetworkReply* reply = qobject_cast<QNetworkReply*>(sender());

    QString response;
    if (reply) {
        if (reply->error() == QNetworkReply::NoError) {
            const int available = reply->bytesAvailable();
            if (available > 0) {
                const QByteArray buffer(reply->readAll());
                response = QString::fromUtf8(buffer);

                parse(response);
            }
        } else {
            qDebug() << "reply... " << reply->errorString();
            emit failedRenew();
        }

        reply->deleteLater();
    }

}


void GoogleConnectController::parse(const QString &message) {
    //qDebug() << message.mid(0, 3);
    //qDebug() << message.mid(5);

    QRegExp access_token("\"access.token\"[: ]+\"([^\"]+)\"");
    access_token.setMinimal(true);

    QRegExp expires_in("\"expires.in\"[: ]+([0-9]+),");
    expires_in.setMinimal(true);

    QRegExp refresh_token("\"refresh.token\"[: ]+\"([^\"]+)\"");
    refresh_token.setMinimal(true);

    if(access_token.indexIn(message) != -1)
        m_Settings->setValue("access_token", access_token.cap(1));

    if(expires_in.indexIn(message) != -1)
            m_Settings->setValue("expires_in", expires_in.cap(1));

    if(refresh_token.indexIn(message) != -1)
            m_Settings->setValue("refresh_token", refresh_token.cap(1));

    // Stop facebook synch
    m_Settings->setValue("Facebook_access_token", "");
    m_Settings->setValue("Facebook_expires_in", "");

}



void GoogleConnectController::checkRefresh() {
    QNetworkReply* reply = qobject_cast<QNetworkReply*>(sender());

    QString response;
    if (reply) {
        if (reply->error() == QNetworkReply::NoError) {
            const int available = reply->bytesAvailable();
            if (available > 0) {
                const QByteArray buffer(reply->readAll());
                response = QString::fromUtf8(buffer);

                parseRefresh(response);
            }
        } else {
            qDebug() << "reply... " << reply->errorString();
            emit failedRenew();
        }

        reply->deleteLater();
    }
    qDebug() << "quit...";
}


void GoogleConnectController::parseRefresh(const QString &message) {
    //qDebug() << message.mid(0, 3);
    //qDebug() << message.mid(5);

    QRegExp access_token("\"access.token\"[: ]+\"([^\"]+)\"");
    access_token.setMinimal(true);

    QRegExp expires_in("\"expires.in\"[: ]+([0-9]+),");
    expires_in.setMinimal(true);


    if(access_token.indexIn(message) != -1)
        m_Settings->setValue("access_token", access_token.cap(1));

    if(expires_in.indexIn(message) != -1)
        m_Settings->setValue("expires_in", expires_in.cap(1));


    emit tokenObtained(access_token.cap(1));

}
