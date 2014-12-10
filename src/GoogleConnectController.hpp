/*
 * GoogleConnectController.hpp
 *
 *  Created on: 19 oct. 2014
 *      Author: pierre
 */

#ifndef GOOGLECONNECTCONTROLLER_HPP_
#define GOOGLECONNECTCONTROLLER_HPP_

#include <QtCore/QObject>
#include <bb/cascades/WebView>
#include <QSettings>

class GoogleConnectController : public QObject {
    Q_OBJECT;


private:
    QSettings                           *m_Settings;

public:
     GoogleConnectController            (QObject *parent = 0);
     virtual ~GoogleConnectController   ()                      {};


     void getToken              ();
     void parse                 (const QString &message);
     void parseRefresh          (const QString &message);

private:

public Q_SLOTS:
    void checkReply             ();
    void checkRefresh           ();
    void renewToken            ();

Q_SIGNALS:

    void tokenObtained          (const QString &token);
    void failedRenew            ();

};




#endif /* GOOGLECONNECTCONTROLLER_HPP_ */
