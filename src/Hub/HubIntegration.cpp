/*
 * HubIntegration.cpp
 *
 *  Created on: 24 mai 2014
 *      Author: pierre
 */

#include <math.h>

#include "HubIntegration.hpp"

#include <QDebug>

HubIntegration::HubIntegration(UDSUtil* udsUtil, HubCache* hubCache) : HubAccount(udsUtil, hubCache) {

    _categoryId = 0;

    _name = "Hg10";
    _displayName = "Hg10";
    _description = "Messages";
    _serverName = "";
    _iconFilename = "images/Hg10Hub.png";
    _lockedIconFilename = "images/Hg10Hub.png";
    _composeIconFilename = "images/Hg10Hub.png";
    _supportsCompose = false;
    _supportsMarkRead = true;
    _supportsMarkUnread = true;
    _headlessTarget = "com.amonchakai.Hg10Service";
    _appTarget = "com.amonchakai.Hg10";
    _cardTarget = "com.amonchakai.Hg10.card";
    _itemMimeType = "hub/vnd.hg10.item";  // mime type for hub items - if you change this, adjust invocation targets
                                            // to match and ensure this is unique for your application or you might invoke the wrong card
    _itemComposeIconFilename = "images/icon_write.png";
    _itemReadIconFilename = "images/itemRead.png";
    _itemUnreadIconFilename = "images/itemUnread.png";
    _markReadActionIconFilename = "images/icon_MarkRead.png";
    _markUnreadActionIconFilename = "images/icon_MarkUnread.png";


    // on device restart / update, it may be necessary to reload the Hub
    if (_udsUtil->reloadHub()) {
        _udsUtil->cleanupAccountsExcept(-1, _displayName);
        _udsUtil->initNextIds();
    }


    initialize();

    QVariantList categories;
    QVariantMap category;
    category["categoryId"] = 1; // categories are created with sequential category Ids starting at 1 so number your predefined categories
                                // accordingly
    category["name"] = "Inbox";
    category["parentCategoryId"] = 0; // default parent category ID for root categories
    categories << category;

    initializeCategories(categories);


    // reload existing hub items if required
    if (_udsUtil->reloadHub()) {
        repopulateHub();

        _udsUtil->resetReloadHub();
    }

    qDebug() << "HubIntegration: initialization done!";
}


HubIntegration::~HubIntegration() {

}

void HubIntegration::removeAccounts() {
    _udsUtil->cleanupAccountsExcept(_accountId, _name);
    remove();
}


qint64 HubIntegration::accountId()
{
    return _accountId;
}

qint64 HubIntegration::categoryId()
{
    return _categoryId;
}

void HubIntegration::initializeCategories(QVariantList newCategories)
{
    HubAccount::initializeCategories(newCategories);

    if (_categoriesInitialized) {
        // initialize category ID - we are assuming that we only added one category
        QVariantList categories = _hubCache->categories();

        _categoryId = categories[0].toMap()["categoryId"].toLongLong();
    }
}


