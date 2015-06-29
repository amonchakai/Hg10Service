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
#include "QXmppVCardIq.h"
#include "Hub/HubIntegration.hpp"
#include "Hub/HubCache.hpp"
#include "../../Hg10/src/DataObjects.h"


extern "C" {
    #include <proto.h>
    #include <privkey.h>
    #include <message.h>
    #include <context.h>
    #include <instag.h>
    #include <gcrypt.h>
}



using namespace bb::cascades;

HeadlessApplication::HeadlessApplication(bb::Application *app) :
        QObject(app),
        m_InvokeManager(new bb::system::InvokeManager()),
        m_app(app),
        m_UdsUtil(NULL),
        m_Settings(NULL),
        m_HubCache(NULL),
        m_Hub(NULL) {

    m_InvokeManager->setParent(this);

    // ---------------------------------------------------------------------
    // prepare to process events

    qDebug() << "-----------------------------------\nStart Headless app!...\n------------------------------------";


    OTRL_INIT;


    qDebug() << "initializeHub()";
    initializeHub();

    // ---------------------------------------------------------------------
    // Catch events

    qDebug() << "connect invoke framework";
    bool connectResult;

    Q_UNUSED(connectResult);
    connectResult = connect( m_InvokeManager,
             SIGNAL(invoked(const bb::system::InvokeRequest&)),
             this,
             SLOT(onInvoked(const bb::system::InvokeRequest&)));

    Q_ASSERT(connectResult);

    qDebug() << "connected invoke framework";

    QTimer::singleShot(3000, this, SLOT(delayedXMPPInit()));

}

void HeadlessApplication::delayedXMPPInit() {
    XMPP::get()->m_Hub = m_Hub;
    XMPP::get()->m_App = this;

    qDebug() << "connect signal()";
    bool check = QObject::connect(XMPP::get(), SIGNAL(updateHubAccount()), this, SLOT(resynchHub()));
    Q_ASSERT(check);
    Q_UNUSED(check);
    qDebug() << "connected signal";


    // ---------------------------------------------------------
    // attach Internet connection status signals

    check = QObject::connect(&m_NetworkStatus, SIGNAL(connectedChanged(bool)), this, SLOT(connectedChanged(bool)));
    Q_ASSERT(check);
}


void HeadlessApplication::connectedChanged(bool connected) {
    if(!connected) {
        XMPP::get()->waitForInternet();
    } else {
        XMPP::get()->internetIsBack();
    }
}


void HeadlessApplication::onInvoked(const bb::system::InvokeRequest& request) {
    qDebug() << "invoke Headless!" << request.action();
    initializeHub();

    if(request.action().compare("bb.action.system.STARTED") == 0) {
            qDebug() << "HeadlessHubIntegration: onInvoked: HeadlessHubIntegration : auto started";
        } else if(request.action().compare("bb.action.START") == 0) {
            qDebug() << "HeadlessHubIntegration: onInvoked: HeadlessHubIntegration : start";
            XMPP::get()->waitRemote();
        } else if(request.action().compare("bb.action.STOP") == 0) {
            qDebug() << "HeadlessHubIntegration: onInvoked: HeadlessHubIntegration : stop";
            m_app->requestExit();

        } else if(request.action().compare("bb.action.MARKREAD") == 0) {
            //qDebug() << "HeadlessHubIntegration: onInvoked: mark read" << request.data();
            bb::data::JsonDataAccess jda;

            QVariantMap objectMap = (jda.loadFromBuffer(request.data())).toMap();
            QVariantMap attributesMap = objectMap["attributes"].toMap();

            {

                QString directory = QDir::homePath() + QLatin1String("/ApplicationData/History");
                TimeEvent e;
                QString filename = directory + "/" + attributesMap["userData"].toString() + ".preview";
                qDebug() << filename;

                QFile file2(filename);
                if (file2.open(QIODevice::ReadOnly)) {
                    QDataStream stream(&file2);
                    stream >> e;
                    file2.close();

                    e.m_Read = 1;
                    attributesMap["description"] = e.m_What;

                    QFile file(filename);
                    if(file.open(QIODevice::WriteOnly)) {
                        QDataStream stream(&file);
                        stream << e;
                        file.close();
                    }
                }

                markHubItemRead(attributesMap);
            }


        } else if(request.action().compare("bb.action.MARKUNREAD") == 0) {
            //qDebug() << "HeadlessHubIntegration: onInvoked: mark unread" << request.data();
            bb::data::JsonDataAccess jda;

            QVariantMap objectMap = (jda.loadFromBuffer(request.data())).toMap();
            QVariantMap attributesMap = objectMap["attributes"].toMap();

            {

                QString directory = QDir::homePath() + QLatin1String("/ApplicationData/History");
                TimeEvent e;
                QString filename = directory + "/" + attributesMap["userData"].toString() + ".preview";
                qDebug() << filename;

                QFile file2(filename);
                if (file2.open(QIODevice::ReadOnly)) {
                    QDataStream stream(&file2);
                    stream >> e;
                    file2.close();

                    e.m_Read = 0;

                    QFile file(filename);
                    if(file.open(QIODevice::WriteOnly)) {
                        QDataStream stream(&file);
                        stream << e;
                        file.close();
                    }
                }

                markHubItemUnread(attributesMap);
            }

        } else if(request.action().compare("bb.action.MARKPRIORREAD") == 0) {
            bb::data::JsonDataAccess jda;

            qint64 timestamp = (jda.loadFromBuffer(request.data())).toLongLong();

            QVariantList hubItems = m_Hub->items();
            for(int i = 0 ; i < hubItems.size() ; ++i) {
                QVariantMap item = hubItems.at(i).toMap();

                if(item["timestamp"].toLongLong() < timestamp) {
                    QString directory = QDir::homePath() + QLatin1String("/ApplicationData/History");
                    TimeEvent e;
                    QString filename = directory + "/" + item["userData"].toString() + ".preview";
                    qDebug() << filename;

                    QFile file2(filename);
                    if (file2.open(QIODevice::ReadOnly)) {
                        QDataStream stream(&file2);
                        stream >> e;
                        file2.close();

                        e.m_Read = 1;

                        QFile file(filename);
                        if(file.open(QIODevice::WriteOnly)) {
                            QDataStream stream(&file);
                            stream << e;
                            file.close();
                        }
                    }

                    markHubItemRead(item);
                }
            }


        } else if(request.action().compare("bb.action.DELETE") == 0) {
            // qDebug() << "HeadlessHubIntegration: onInvoked: HeadlessHubIntegration : delete" << request.data();
            bb::data::JsonDataAccess jda;

            QVariantMap objectMap = (jda.loadFromBuffer(request.data())).toMap();
            QVariantMap attributesMap = objectMap["attributes"].toMap();

            {
                QString directory = QDir::homePath() + QLatin1String("/ApplicationData/History");
                TimeEvent e;
                if (QFile::exists(directory + "/" + attributesMap["userData"].toString() + ".preview")) {
                    QDir dir(directory);
                    dir.remove(attributesMap["userData"].toString() + ".preview");
                }
                removeHubItem(attributesMap);
            }

        } else if(request.action().compare("bb.action.DELETEPRIOR") == 0) {
            bb::data::JsonDataAccess jda;

            qint64 timestamp = (jda.loadFromBuffer(request.data())).toLongLong();
            QVariantList hubItems = m_Hub->items();
            for(int i = 0 ; i < hubItems.size() ; ++i) {
                QVariantMap item = hubItems.at(i).toMap();

                if(item["timestamp"].toLongLong() < timestamp) {
                    QString directory = QDir::homePath() + QLatin1String("/ApplicationData/History");
                    TimeEvent e;
                    if (QFile::exists(directory + "/" + item["userData"].toString() + ".preview")) {
                        QDir dir(directory);
                        dir.remove(item["userData"].toString() + ".preview");
                    }
                    removeHubItem(item);
                }
            }


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

    m_Hub->markHubItemRead(m_Hub->categoryId(), itemId);
}

void HeadlessApplication::markHubItemUnread(QVariantMap itemProperties) {

    qint64 itemId;

    if (itemProperties["sourceId"].toString().length() > 0) {
        itemId = itemProperties["sourceId"].toLongLong();
    } else if (itemProperties["messageid"].toString().length() > 0) {
        itemId = itemProperties["messageid"].toLongLong();
    }

    m_Hub->markHubItemUnread(m_Hub->categoryId(), itemId);
}

void HeadlessApplication::removeHubItem(QVariantMap itemProperties) {

    qint64 itemId;
    if (itemProperties["sourceId"].toString().length() > 0) {
        itemId = itemProperties["sourceId"].toLongLong();
    } else if (itemProperties["messageid"].toString().length() > 0) {
        itemId = itemProperties["messageid"].toLongLong();
    }

    m_Hub->removeHubItem(m_Hub->categoryId(), itemId);
}


void HeadlessApplication::initializeHub() {
//    return;

    m_InitMutex.lock();

    // initialize UDS
    if (!m_UdsUtil) {
        qDebug() << "new UDSUtil()";
        m_UdsUtil = new UDSUtil(QString("Hg10HubService"), QString("hubassets"));
    }

    if (!m_UdsUtil->initialized()) {
        qDebug() << "initialize()";
        m_UdsUtil->initialize();
    }

    if (m_UdsUtil->initialized() && m_UdsUtil->registered()) {
        qDebug() << "m_UdsUtil->initialized() && m_UdsUtil->registered()";
        if (!m_Settings) {

            m_Settings = new QSettings("Amonchakai", "Hg10Service");
        }
        if (!m_HubCache) {
            m_HubCache = new HubCache(m_Settings);
        }
        if (!m_Hub) {
            qDebug() << "new HubIntegration()";
            m_Hub = new HubIntegration(m_UdsUtil, m_HubCache);
        }
    }

    qDebug() << "done initializeHub()";
    m_InitMutex.unlock();
}


// ------------------------------------------------------------------------------------
// List all conversations, and update the Hub

void HeadlessApplication::resynchHub() {

    if(m_Hub == NULL) {
        return;
    }

    // items already into the hub
    QVariantList hubItems = m_Hub->items();


    // available conversations
    QString directory = QDir::homePath() + QLatin1String("/ApplicationData/History");
    if (QFile::exists(directory)) {
        QDir dir(directory);
        dir.setNameFilters(QStringList() << "*.preview");
        dir.setFilter(QDir::Files);
        foreach(QString dirFile, dir.entryList()) {

            QFile file2(directory + "/" + dirFile);

            TimeEvent e;
            if (file2.open(QIODevice::ReadOnly)) {
                QDataStream stream(&file2);
                stream >> e;

                file2.close();
            }

            e.m_What.replace("&#39;","\'");

            bool existing = false;
            QVariantMap itemMap;
            for(int i = 0 ; i < hubItems.length() ; ++i) {
                itemMap = hubItems.at(i).toMap();

                if(itemMap["userData"].toString() == dirFile.mid(0, dirFile.length()-8)) {
                    existing = true;
                    break;
                }
            }

            if(existing) {
                if(itemMap["timestamp"].toULongLong() == e.m_When)
                    continue;

                itemMap["description"] = e.m_What;
                itemMap["timestamp"] = e.m_When;
                itemMap["readCount"] = e.m_Read;

                qint64 itemId;
                if (itemMap["sourceId"].toString().length() > 0) {
                    itemId = itemMap["sourceId"].toLongLong();
                } else if (itemMap["messageid"].toString().length() > 0) {
                    itemId = itemMap["messageid"].toLongLong();
                }


                m_Hub->updateHubItem(m_Hub->categoryId(), itemId, itemMap, e.m_Read == 0);


            } else {
                // addHubItem(qint64 categoryId, QVariantMap &itemMap, QString name, QString subject, qint64 timestamp, QString itemSyncId,  QString itemUserData, QString itemExtendedData, bool notify)
                QVariantMap entry;
                entry["userData"] = dirFile.mid(0, dirFile.length()-8);


                QString name;
                {
                    // -------------------------------------------------------------
                    // get vCard from file
                    QString vCardsDir = QDir::homePath() + QLatin1String("/vCards");
                    QFile file(vCardsDir + "/" + entry["userData"].toString() + ".xml");

                    QDomDocument doc("vCard");
                    file.open(QIODevice::ReadOnly);

                    doc.setContent(&file);
                    file.close();

                    QXmppVCardIq vCard;
                    vCard.parse(doc.documentElement());

                    name = vCard.fullName();


                    // I should not have empty items!
                    if(name.isEmpty()) {
                        name = vCard.nickName();

                        if(name.isEmpty())
                            return;
                    }


                }

                QString email = dirFile.mid(0, dirFile.length()-8);
                // I should not have empty email addresses!
                if(email.isEmpty())
                    return;

                m_Hub->addHubItem(m_Hub->categoryId(), entry, name, e.m_What, e.m_When, email, email, "",  e.m_Read == 0);


                qint64 itemId;
                if (entry["sourceId"].toString().length() > 0) {
                    itemId = entry["sourceId"].toLongLong();
                } else if (entry["messageid"].toString().length() > 0) {
                    itemId = entry["messageid"].toLongLong();
                }

                if(e.m_Read != 0) {
                    m_Hub->markHubItemRead(m_Hub->categoryId(), itemId);
                }
            }


        }
    }
}

