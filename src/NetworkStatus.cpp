/*
 * NetworkStatus.cpp
 *
 *  Created on: 5 juin 2015
 *      Author: pierre
 */

#include "NetworkStatus.hpp"
#include <QTimer>

NetworkStatus::NetworkStatus() : m_Connected(false), m_ConnectedCache(false), m_InterfaceName("Unknown"), m_InterfaceType(NetworkStatus::Unknown) {

    subscribe(netstatus_get_domain());

    bps_initialize();

    // Request all network status events.
    netstatus_request_events(0);

    m_Info = NULL;
}

NetworkStatus::~NetworkStatus() {
    bps_shutdown();
}


void NetworkStatus::event(bps_event_t *event) {
    bool status = false;
    const char* interface = "";
    Type type = Unknown;
    netstatus_interface_details_t* details = NULL;

    // Verify that the event coming in is a network status event.
    if (bps_event_get_domain(event) == netstatus_get_domain()) {
        // Using the BPS event code of the network status event,
        // verify that the event is a network information event.
        if (NETSTATUS_INFO == bps_event_get_code(event)) {
            // Retrieve the network status information, and verify
            // that the procedure is successful.
            if (BPS_SUCCESS == netstatus_get_info(&m_Info)) {
                status = netstatus_info_get_availability(m_Info);
                interface = netstatus_info_get_default_interface(m_Info);
                int success = netstatus_get_interface_details(interface,
                        &details);

                if (success == BPS_SUCCESS) {
                    switch (netstatus_interface_get_type(details)) {
                    case NETSTATUS_INTERFACE_TYPE_WIRED:
                        type = Wired;
                        break;

                    case NETSTATUS_INTERFACE_TYPE_WIFI:
                        type = Wifi;
                        break;

                    case NETSTATUS_INTERFACE_TYPE_BLUETOOTH_DUN:
                        type = Bluetooth;
                        break;

                    case NETSTATUS_INTERFACE_TYPE_USB:
                    case NETSTATUS_INTERFACE_TYPE_BB:
                        type = Usb;
                        break;

                    case NETSTATUS_INTERFACE_TYPE_VPN:
                        type = VPN;
                        break;

                    case NETSTATUS_INTERFACE_TYPE_CELLULAR:
                        type = Cellular;
                        break;

                    case NETSTATUS_INTERFACE_TYPE_P2P:
                    case NETSTATUS_INTERFACE_TYPE_UNKNOWN:
                        type = Unknown;
                        break;
                    }
                    netstatus_free_info(&m_Info);
                }
            }

            // Emit the signal to trigger networkStatusUpdated slot.
            this->setConnected(status);
            this->setInterfaceType(type);
        }
    }
}
bool NetworkStatus::isConnected() {
    return m_Connected;
}
QString NetworkStatus::interfaceName() {
    return m_InterfaceName;
}

NetworkStatus::Type NetworkStatus::interfaceType() {
    return m_InterfaceType;
}

void NetworkStatus::setConnected(bool connected) {
    if (connected != m_Connected) {
        m_ConnectedCache = connected;

        // if there is no access, wait 1 min in case the access could come back.
        if(!connected)
            QTimer::singleShot(20*1000, this, SLOT(registerConnectionLoss()));
        else {
            m_Connected = connected;
            emit connectedChanged(m_Connected);
        }
    }
}

// wait 1min, before reporting loss of connection.
void NetworkStatus::registerConnectionLoss() {
    if(!m_ConnectedCache) {
        m_Connected = m_ConnectedCache;
        emit connectedChanged(m_Connected);
    }
}

void NetworkStatus::setInterfaceType(NetworkStatus::Type interfaceType) {
    if (interfaceType != m_InterfaceType) {
        m_InterfaceType = interfaceType;
        QString name;
        switch (interfaceType) {
        case NetworkStatus::Wired:
            name = "Wired";
            break;
        case NetworkStatus::Wifi:
            name = "Wifi";
            break;
        case NetworkStatus::Bluetooth:
            name = "Bluetooth";
            break;
        case NetworkStatus::Usb:
            name = "Usb";
            break;
        case NetworkStatus::VPN:
            name = "VPN";
            break;
        case NetworkStatus::Cellular:
            name = "Cellular";
            break;
        default:
            name = "Unknown";
        }
        emit interfaceTypeChanged(m_InterfaceType);
        m_InterfaceName = name;
        emit interfaceNameChanged(m_InterfaceName);
    }
}
