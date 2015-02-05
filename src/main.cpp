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

#include <bb/Application>

#include <QLocale>
#include <QTranslator>

#include <Qt/qdeclarativedebug.h>
#include "XMPPService.hpp"

#include "Hub/HubIntegration.hpp"
#include "Hub/UDSUtil.hpp"
#include "Hub/HubCache.hpp"

void debugOutputMessages(QtMsgType type, const char *msg) {

    QString directory = QDir::homePath() + QLatin1String("/ApplicationData");
    if (!QFile::exists(directory)) {
        QDir dir;
        dir.mkpath(directory);
    }

    QFile file(directory + "/Logs.txt");
    if (!file.open(QIODevice::Append)) {
        return;
    }
    QTextStream stream(&file);

    switch (type) {
        case QtDebugMsg:
            stream << "<div class=\"debug\">[DEBUG]"  <<  msg << "</div>";
            break;
        case QtWarningMsg:
            stream << "<div class=\"warning\">[WARNING]"  <<  msg << "</div>";
            break;
        case QtCriticalMsg:
            stream << "<div class=\"critical\">[CRITICAL]"  <<  msg << "</div>";
            break;
        case QtFatalMsg:
            stream << "<div class=\"fatal\">[FATAL]"  <<  msg << "</div>";
            file.close();
            abort();
    }

    file.close();

 }


Q_DECL_EXPORT int main(int argc, char **argv)
{
    // redirect log to a file...
    //qInstallMsgHandler(debugOutputMessages);
    bb::Application app(argc, argv);

    // create the xmpp client, allocate the XMPP client on the stack
    XMPP xmpp;
    xmpp.set(&xmpp);

    // Create Headless application.
    HeadlessApplication headless(&app);

    // Enter the application main event loop.
    return bb::Application::exec();
}
