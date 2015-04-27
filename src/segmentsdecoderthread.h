/***************************************************************************
 *   Copyright (C) 2009 by Xavier Lefage                                   *
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

#ifndef SEGMENTSDECODERTHREAD_H
#define SEGMENTSDECODERTHREAD_H

#include <QThread>
#include "data/nzbfiledata.h"
#include "data/postdownloadinfodata.h"
#include "utilities/utility.h"
using namespace UtilityNamespace;

class Core;
class SegmentDecoderBase;
class SegmentDecoderYEnc;
class SegmentDecoderUUEnc;
class SegmentData;

class SegmentsDecoderThread : public QObject {

    Q_OBJECT

public:

    SegmentsDecoderThread(Core*);
    SegmentsDecoderThread();
    ~SegmentsDecoderThread();
    void emitDecodeProgression(const PostDownloadInfoData&);
    void emitSaveFileError();

private:
    QThread* dedicatedThread;
    Core* parent;
    SegmentDecoderYEnc* yencDecoder;
    SegmentDecoderUUEnc* uuencDecoder;
    QList<SegmentData> segmentDataList;

    void init();
    void setupConnections();
    SegmentDecoderYEnc* retrieveYencDecoder();
    SegmentDecoderBase* retrieveProperDecoderAfterDownload(const NzbFileData&, QString&);
    SegmentDecoderBase* retrieveProperDecoderDuringDownload(SegmentData&, QString&);


Q_SIGNALS:
    void updateDecodeSegmentSignal(SegmentData, int, int);
    void updateDecodeSignal(PostDownloadInfoData);
    void saveFileErrorSignal(int);
    void updateDownloadSegmentSignal(SegmentData, QString);
    void segmentDecoderIdleSignal();
    void finalizeDecoderIdleSignal();


public Q_SLOTS:
    void decodeSegmentsSlot(NzbFileData);
    void suppressOldOrphanedSegmentsSlot();
    void saveDownloadedSegmentSlot(SegmentData);
};

#endif // SEGMENTSDECODERTHREAD_H
