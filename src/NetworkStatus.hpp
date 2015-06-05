/*
 * NetworkStatus.hpp
 *
 *  Created on: 5 juin 2015
 *      Author: pierre
 */

#ifndef NETWORKSTATUS_HPP_
#define NETWORKSTATUS_HPP_

#include <QObject>
#include <QString>
#include <bb/AbstractBpsEventHandler>
#include <bps/netstatus.h>

class NetworkStatus : public QObject, public bb::AbstractBpsEventHandler {
    Q_OBJECT;



public:

    enum Type {
        Wired, Wifi, Bluetooth, Usb, VPN, Cellular, Unknown
    };



private:
    netstatus_info_t* m_Info;
    bool m_Connected;
    bool m_ConnectedCache;
    QString m_InterfaceName;
    Type m_InterfaceType;



    void setConnected           (bool connected);
    void setInterfaceType       (Type interfaceType);




public:

    NetworkStatus();
    virtual ~NetworkStatus();





    virtual void    event(bps_event_t *event);
    bool            isConnected();
    QString         interfaceName();
    Type            interfaceType();


public Q_SLOTS:
    void registerConnectionLoss     ();

signals:
    void connectedChanged           (bool connected);
    void interfaceNameChanged       (QString interfaceName);
    void interfaceTypeChanged       (NetworkStatus::Type interfaceType);


};


#endif /* NETWORKSTATUS_HPP_ */
