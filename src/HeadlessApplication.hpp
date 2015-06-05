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

#ifndef HEADLESS_APPLICATION
#define HEADLESS_APPLICATION

#include <bb/system/InvokeManager>
#include <bb/Application>
#include <QObject>
#include <QSettings>

#include "Hub/UDSUtil.hpp"
#include "Hub/HubCache.hpp"
#include "NetworkStatus.hpp"

namespace bb
{
    namespace cascades
    {
        class LocaleHandler;
    }
}

class HubIntegration;

/*!
 * @brief Application UI object
 *
 * Use this object to create and init app UI, to create context objects, to register the new meta types etc.
 */
class HeadlessApplication : public QObject
{
    Q_OBJECT;

private:

    bb::system::InvokeManager                       *m_InvokeManager;
    bb::Application                                 *m_app;
    UDSUtil                                         *m_UdsUtil;
    QSettings                                       *m_Settings;
    HubCache                                        *m_HubCache;
    HubIntegration                                  *m_Hub;

    QMutex                                           m_InitMutex;
    NetworkStatus                                    m_NetworkStatus;

public:
    HeadlessApplication(bb::Application *app);
    virtual ~HeadlessApplication() {}
    void initializeHub();

public Q_SLOTS:
    void resynchHub();
    void delayedXMPPInit();
    void connectedChanged(bool connected);

private slots:
    void onInvoked(const bb::system::InvokeRequest& request);


    void markHubItemRead(QVariantMap itemProperties);
    void markHubItemUnread(QVariantMap itemProperties);
    void removeHubItem(QVariantMap itemProperties);



};

#endif /* HEADLESS_APPLICATION */
