/***************************************************************************
 *   Copyright (C) 2010 by Xavier Lefage                                   *
 *   xavier.kwooty@gmail.com                                               *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 *   This program is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU General Public License for more details.                          *
 *                                                                         *
 *   You should have received a copy of the GNU General Public License     *
 *   along with this program; if not, write to the                         *
 *   Free Software Foundation, Inc.,                                       *
 *   59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.             *
 ***************************************************************************/

#ifndef QUEUEFILEOBSERVER_H
#define QUEUEFILEOBSERVER_H

#include <QObject>
#include <QStandardItem>
#include <QMap>
#include <QDateTime>
#include <QTimer>

#include "data/jobnotifydata.h"
#include "kwooty_export.h"
#include "utilities/utility.h"
using namespace UtilityNamespace;

class Core;
class StandardItemModel;
class StandardItemModelQuery;
class MyTreeView;

class KWOOTY_EXPORT QueueFileObserver : public QObject
{

    Q_OBJECT

public:

    explicit QueueFileObserver(Core *);
    UtilityNamespace::ItemStatus getFocusedItemStatus() const;
    int getFocusedProgressValue() const;
    bool isPluginJobRunning();

private:

    static const int MAX_LIST_SIZE = 10;

    StandardItemModel *downloadModel;
    StandardItemModelQuery *modelQuery;
    MyTreeView *treeView;
    QStandardItem *parentItem;
    QTimer *jobNotifyTimer;
    QList<JobNotifyData> jobNotifyDataList;
    UtilityNamespace::ItemStatus focusedItemStatus;
    int focusedProgressValue;
    int previousProgressValue;
    bool pluginJobRunning;

    void setupConnections();

    void checkProgressItemValue(QStandardItem *);
    JobNotifyData retrieveJobNotifyData(QStandardItem *, UtilityNamespace::ItemStatus);
    void addToList(const JobNotifyData &);

Q_SIGNALS:

    void progressUpdateSignal(const int);
    void statusUpdateSignal(const UtilityNamespace::ItemStatus);
    void jobFinishSignal(const UtilityNamespace::ItemStatus, const QString &);

public Q_SLOTS:

    void parentItemChangedSlot();
    void jobFinishStatusSlot(QStandardItem *);
    void pluginJobRunningSlot(bool);

private Q_SLOTS:

    void checkJobFinishSlot();

};

#endif // QUEUEFILEOBSERVER_H
