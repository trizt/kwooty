/***************************************************************************
 *   Copyright (C) 2011 by Xavier Lefage                                   *
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

#ifndef SEGMENTBUFFER_H
#define SEGMENTBUFFER_H

#include <QObject>

#include "segmentsdecoderthread.h"
#include "data/segmentdata.h"
#include "data/nzbfiledata.h"

#include "utilities/utility.h"
using namespace UtilityNamespace;

class ServerManager;
class Core;

class SegmentBuffer : public QObject
{

    Q_OBJECT

public:
    SegmentBuffer(ServerManager *, Core *);
    int segmentSavingQueued(const SegmentData &);
    void finalizeDecodeQueued(const NzbFileData &);
    void lockFinalizeDecode();
    void unlockFinalizeDecode();
    bool isfinalizeDecodeIdle() const;
    bool isBufferFull() const;
    void updateDecodeWaitingQueue(const NzbFileData &, const NzbFileData &);
    void removeDataFromDecodeWaitingQueue(const NzbFileData &);

private:

    QList<SegmentData> mSegmentDataList;
    QList<NzbFileData> mnZbFileDataList;
    Core *mCore;
    ServerManager *mServerManager;
    qint64 mDataSizeCounter;
    int mRequestNextSegmentDelaySec;
    bool mSegmentDecoderIdle;
    bool mFinalizeLocked;
    bool mFinalizeDecodeIdle;
    bool mBufferFull;

    void setupConnections();
    void sendDataToFinalizeDecode();

Q_SIGNALS:
    void saveDownloadedSegmentSignal(const SegmentData &);
    void decodeSegmentsSignal(const NzbFileData &);
    void finalizeDecoderLockedSignal();

public Q_SLOTS:
    void segmentDecoderIdleSlot();
    void finalizeDecodeQueuedSlot(const NzbFileData &);
    void finalizeDecoderIdleSlot();
};

#endif // SEGMENTBUFFER_H
