/*
 * Copyright (c) 2011-2014 BlackBerry Limited.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "HeadlessApplication.hpp"

#include <bb/cascades/Application>
#include <bb/cascades/QmlDocument>
#include <bb/cascades/AbstractPane>
#include <bb/cascades/LocaleHandler>
#include <bb/data/JsonDataAccess>

#include "XMPPService.hpp"

using namespace bb::cascades;

HeadlessApplication::HeadlessApplication(bb::Application *app) :
        QObject(app),
        m_InvokeManager(new bb::system::InvokeManager()),
        m_app(app),
        m_AppSettings(NULL) {

    m_InvokeManager->setParent(this);

    // ---------------------------------------------------------------------
    // HUB integration



    // ---------------------------------------------------------------------
    // prepare to process events

    qDebug() << "-----------------------------------\nStart Headless app!...\nWait for ticks\n------------------------------------";

    XMPP::get();



    // ---------------------------------------------------------------------
    // Catch events

    bool connectResult;

    Q_UNUSED(connectResult);
    connectResult = connect( m_InvokeManager,
             SIGNAL(invoked(const bb::system::InvokeRequest&)),
             this,
             SLOT(onInvoked(const bb::system::InvokeRequest&)));

    Q_ASSERT(connectResult);

}


void HeadlessApplication::onInvoked(const bb::system::InvokeRequest& request) {
    qDebug() << "invoke Headless!" << request.action();

    if(request.action().compare("bb.action.system.STARTED") == 0) {
            qDebug() << "HeadlessHubIntegration: onInvoked: HeadlessHubIntegration : auto started";
        } else if(request.action().compare("bb.action.START") == 0) {
            XMPP::get()->waitRemote();
            qDebug() << "HeadlessHubIntegration: onInvoked: HeadlessHubIntegration : start";
        } else if(request.action().compare("bb.action.STOP") == 0) {
            qDebug() << "HeadlessHubIntegration: onInvoked: HeadlessHubIntegration : stop";
            m_app->requestExit();

        } else if(request.action().compare("bb.action.MARKREAD") == 0) {
            qDebug() << "HeadlessHubIntegration: onInvoked: mark read" << request.data();
            bb::data::JsonDataAccess jda;

            QVariantMap objectMap = (jda.loadFromBuffer(request.data())).toMap();
            QVariantMap attributesMap = objectMap["attributes"].toMap();


        } else if(request.action().compare("bb.action.MARKUNREAD") == 0) {
            qDebug() << "HeadlessHubIntegration: onInvoked: mark unread" << request.data();
            bb::data::JsonDataAccess jda;

            QVariantMap objectMap = (jda.loadFromBuffer(request.data())).toMap();
            QVariantMap attributesMap = objectMap["attributes"].toMap();


        } else if(request.action().compare("bb.action.MARKPRIORREAD") == 0) {
            bb::data::JsonDataAccess jda;

            qint64 timestamp = (jda.loadFromBuffer(request.data())).toLongLong();
            QDateTime date = QDateTime::fromMSecsSinceEpoch(timestamp);

            qDebug() << "HeadlessHubIntegration: onInvoked: mark prior read : " << timestamp << " : " << request.data();

        } else if(request.action().compare("bb.action.DELETE") == 0) {
            qDebug() << "HeadlessHubIntegration: onInvoked: HeadlessHubIntegration : delete" << request.data();
            bb::data::JsonDataAccess jda;

            QVariantMap objectMap = (jda.loadFromBuffer(request.data())).toMap();
            QVariantMap attributesMap = objectMap["attributes"].toMap();


        } else if(request.action().compare("bb.action.DELETEPRIOR") == 0) {
            bb::data::JsonDataAccess jda;

            qint64 timestamp = (jda.loadFromBuffer(request.data())).toLongLong();
            QDateTime date = QDateTime::fromMSecsSinceEpoch(timestamp);

            qDebug() << "HeadlessHubIntegration: onInvoked: mark prior delete : " << timestamp << " : " << request.data();

        } else {
            qDebug() << "HeadlessHubIntegration: onInvoked: unknown service request " << request.action() << " : " << request.data();
        }

}



void HeadlessApplication::markHubItemRead(QVariantMap itemProperties) {

    qint64 itemId;

    if (itemProperties["sourceId"].toString().length() > 0) {
        itemId = itemProperties["sourceId"].toLongLong();
    } else if (itemProperties["messageid"].toString().length() > 0) {
        itemId = itemProperties["messageid"].toLongLong();
    }

//    m_Hub->markHubItemRead(m_Hub->getCache()->lastCategoryId(), itemId);
}

void HeadlessApplication::markHubItemUnread(QVariantMap itemProperties) {

    qint64 itemId;

    if (itemProperties["sourceId"].toString().length() > 0) {
        itemId = itemProperties["sourceId"].toLongLong();
    } else if (itemProperties["messageid"].toString().length() > 0) {
        itemId = itemProperties["messageid"].toLongLong();
    }

//    m_Hub->markHubItemUnread(m_Hub->getCache()->lastCategoryId(), itemId);
}

void HeadlessApplication::removeHubItem(QVariantMap itemProperties) {

    qint64 itemId;
    if (itemProperties["sourceId"].toString().length() > 0) {
        itemId = itemProperties["sourceId"].toLongLong();
    } else if (itemProperties["messageid"].toString().length() > 0) {
        itemId = itemProperties["messageid"].toLongLong();
    }

//    m_Hub->removeHubItem(m_Hub->getCache()->lastCategoryId(), itemId);
}


