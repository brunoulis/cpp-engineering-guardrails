// Copyright (C) 2021 The Qt Company Ltd.
// Copyright (C) 2016 Intel Corporation.
// Copyright (C) 2014 Keith Gardner <kreios4004@gmail.com>
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR LGPL-3.0-only OR GPL-2.0-only OR GPL-3.0-only
// Qt-Security score:critical reason:data-parser

#include <QtCore/qversionnumber.h>
#include <QtCore/qhash.h>
#include <QtCore/private/qlocale_tools_p.h>
#include <QtCore/qcollator.h>

#ifndef QT_NO_DATASTREAM
#  include <QtCore/qdatastream.h>
#endif
// Corpus excerpt: intervening original source omitted.
QVersionNumber QVersionNumber::normalized() const
{
    qsizetype i;
    for (i = m_segments.size(); i; --i)
        if (m_segments.at(i - 1) != 0)
            break;

    QVersionNumber result(*this);
    result.m_segments.resize(i);
    return result;
}

/*!
    \fn bool QVersionNumber::isPrefixOf(const QVersionNumber &other) const

    Returns \c true if the current version number is contained in the \a other
    version number, otherwise returns \c false.

    \snippet qversionnumber/main.cpp 2

    \sa commonPrefix()
*/
bool QVersionNumber::isPrefixOf(const QVersionNumber &other) const noexcept
{
    if (segmentCount() > other.segmentCount())
        return false;
    for (qsizetype i = 0; i < segmentCount(); ++i) {
        if (segmentAt(i) != other.segmentAt(i))
            return false;
    }
    return true;
}

/*!
    \fn int QVersionNumber::compare(const QVersionNumber &v1,
                                    const QVersionNumber &v2)

    Compares \a v1 with \a v2 and returns an integer less than, equal to, or
    greater than zero, depending on whether \a v1 is less than, equal to, or
    greater than \a v2, respectively.

    Comparisons are performed by comparing the segments of \a v1 and \a v2
    starting at index 0 and working towards the end of the longer list.

    \snippet qversionnumber/main.cpp 1
*/
int QVersionNumber::compare(const QVersionNumber &v1, const QVersionNumber &v2) noexcept
{
    qsizetype commonlen;

    if (Q_LIKELY(!v1.m_segments.isUsingPointer() && !v2.m_segments.isUsingPointer())) {
        // we can't use memcmp because it interprets the data as unsigned bytes
        const qint8 *ptr1 = v1.m_segments.inline_segments + InlineSegmentStartIdx;
        const qint8 *ptr2 = v2.m_segments.inline_segments + InlineSegmentStartIdx;
        commonlen = qMin(v1.m_segments.size(),
                         v2.m_segments.size());
        for (qsizetype i = 0; i < commonlen; ++i)
            if (int x = ptr1[i] - ptr2[i])
                return x;
    } else {
        commonlen = qMin(v1.segmentCount(), v2.segmentCount());
        for (qsizetype i = 0; i < commonlen; ++i) {
            if (v1.segmentAt(i) != v2.segmentAt(i))
                return v1.segmentAt(i) - v2.segmentAt(i);
        }
    }

    // ran out of segments in v1 and/or v2 and need to check the first trailing
    // segment to finish the compare
    if (v1.segmentCount() > commonlen) {
        // v1 is longer
        if (v1.segmentAt(commonlen) != 0)
            return v1.segmentAt(commonlen);
        else
            return 1;
    } else if (v2.segmentCount() > commonlen) {
        // v2 is longer
        if (v2.segmentAt(commonlen) != 0)
            return -v2.segmentAt(commonlen);
        else
            return -1;
    }

    // the two version numbers are the same
    return 0;
}

/*!
    QVersionNumber QVersionNumber::commonPrefix(const QVersionNumber &v1,
                                                    const QVersionNumber &v2)

    Returns a version number that is a parent version of both \a v1 and \a v2.

    \sa isPrefixOf()
*/
QVersionNumber QVersionNumber::commonPrefix(const QVersionNumber &v1,
                                            const QVersionNumber &v2)
{
    qsizetype commonlen = qMin(v1.segmentCount(), v2.segmentCount());
    qsizetype i;
    for (i = 0; i < commonlen; ++i) {
        if (v1.segmentAt(i) != v2.segmentAt(i))
            break;
    }

    if (i == 0)
        return QVersionNumber();

    // try to use the one with inline segments, if there's one
    QVersionNumber result(!v1.m_segments.isUsingPointer() ? v1 : v2);
    result.m_segments.resize(i);
    return result;
}
