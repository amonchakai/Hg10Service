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

Q_DECL_EXPORT int main(int argc, char **argv)
{
    bb::Application app(argc, argv);

    // create the xmpp client
    XMPP xmpp;
    xmpp.set(&xmpp);

    // Create Headless application.
    HeadlessApplication happ(&app); // it was a new HeadlessApplication(&app);


    // Enter the application main event loop.
    return bb::Application::exec();
}
