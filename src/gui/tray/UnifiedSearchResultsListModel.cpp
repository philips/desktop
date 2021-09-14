/*
 * Copyright (C) by Oleksandr Zolotov <alex@nextcloud.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License
 * for more details.
 */

#include "UnifiedSearchResultsListModel.h"

#include "account.h"
#include "accountstate.h"
#include "networkjobs.h"

#include <algorithm>

namespace OCC {

UnifiedSearchResultsListModel::UnifiedSearchResultsListModel(AccountState *accountState, QObject *parent)
    : QAbstractListModel(parent)
    , _accountState(accountState)
{
}

UnifiedSearchResultsListModel::~UnifiedSearchResultsListModel()
{
    int a = 5;
    a = 6;
}

QVariant UnifiedSearchResultsListModel::data(const QModelIndex &index, int role) const
{
    return QVariant();
}

int UnifiedSearchResultsListModel::rowCount(const QModelIndex &) const
{
    int total = 0;

    for (const auto &category : _resultsByCategory) {
        total += category._results.size();
    }

    return total;
}

void UnifiedSearchResultsListModel::setSearchTerm(const QString &term)
{
    if (term == _searchTerm) {
        return;
    }

    disconnect(&_unifiedSearchTextEditingFinishedTimer, &QTimer::timeout, this, &UnifiedSearchResultsListModel::slotSearchTermEditingFinished);

    if (_unifiedSearchTextEditingFinishedTimer.isActive()) {
        _unifiedSearchTextEditingFinishedTimer.stop();
    }

    _searchTerm = term;

    if (!searchTerm().isEmpty()) {
        _unifiedSearchTextEditingFinishedTimer.setInterval(400);
        connect(&_unifiedSearchTextEditingFinishedTimer, &QTimer::timeout, this, &UnifiedSearchResultsListModel::slotSearchTermEditingFinished);
        _unifiedSearchTextEditingFinishedTimer.start();
    } else {
        beginResetModel();
        _resultsByCategory.clear();
        endResetModel();
    }
}

QString UnifiedSearchResultsListModel::searchTerm() const
{
    return _searchTerm;
}

void UnifiedSearchResultsListModel::slotSearchTermEditingFinished()
{
    disconnect(&_unifiedSearchTextEditingFinishedTimer, &QTimer::timeout, this, &UnifiedSearchResultsListModel::slotSearchTermEditingFinished);
    QString term = searchTerm();

    if (_providers.isEmpty()) {
        auto *job = new JsonApiJob(_accountState->account(), QLatin1String("ocs/v2.php/search/providers"));
        QObject::connect(job, &JsonApiJob::jsonReceived, [&, this](const QJsonDocument &json) {
            const auto providerList = json.object().value("ocs").toObject().value("data").toVariant().toList();

            for (const auto& provider : providerList) {
                const auto providerMap = provider.toMap();
                Provider provider;
                provider._name = providerMap["name"].toString();
                if (!provider._name.isEmpty()) {
                    provider._id = providerMap["id"].toString();
                    provider._order = providerMap["order"].toInt();
                    _providers.insert(provider._name, provider);
                }
            }

            if (!_providers.empty()) {
                startSearch();
            }
        });
        job->start();
    } else {
        startSearch();
    }
}

void UnifiedSearchResultsListModel::startSearch()
{
    beginResetModel();
    _resultsByCategory.clear();
    endResetModel();

    for (const auto& provider : _providers) {
        startSearchForProvider(provider);
    }
}

void UnifiedSearchResultsListModel::startSearchForProvider(const Provider &provider)
{
    auto *job = new JsonApiJob(_accountState->account(), QLatin1String("ocs/v2.php/search/providers/%1/search").arg(provider._id));
    QUrlQuery params;
    params.addQueryItem(QStringLiteral("term"), searchTerm());
    job->addQueryParams(params);
    QObject::connect(job, &JsonApiJob::jsonReceived, [&, provider](const QJsonDocument &json) {
        const auto data = json.object().value("ocs").toObject().value("data").toObject();
        if (!data.isEmpty()) {
            const auto name = data.value("name").toString();
            const auto providerForResults = _providers.find(name);
            const auto isPaginated = data.value("isPaginated").toBool();
            const auto cursor = data.value("cursor").toInt();
            const auto entries = data.value("entries").toVariant().toList();

            if (providerForResults != _providers.end() && !entries.isEmpty()) {
                UnifiedSearchResultCategory category;
                category._order = (*providerForResults)._order;
                category._name = (*providerForResults)._name;

                for (const auto &entry : entries) {
                    UnifiedSearchResult result;
                    result._title = entry.toMap()["title"].toString();
                    category._results.push_back(result);
                }

                beginInsertRows(QModelIndex(), 0, category._results.size() - 1);
                _resultsByCategory.push_back(category);
                endInsertRows();
            }
        }
    });
    job->start();
}

}
