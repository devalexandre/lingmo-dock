/*
 * Copyright (C) 2021 LingmoOS Team.
 *
 * Author:     rekols <revenmartin@gmail.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "processprovider.h"
#include <QDBusInterface>
#include <QDBusPendingCall>
#include <QDBusPendingReply>
#include <QDBusConnectionInterface>
#include <QDebug>
#include <QProcess>

ProcessProvider::ProcessProvider(QObject *parent)
    : QObject(parent)
{

}

bool ProcessProvider::startDetached(const QString &exec, QStringList args)
{
    if (exec.isEmpty()) {
        qWarning() << "ProcessProvider: empty exec";
        return false;
    }

    if (QProcess::startDetached(exec, args)) {
        return true;
    }

    auto bus = QDBusConnection::sessionBus();
    if (bus.interface()
        && bus.interface()->isServiceRegistered(QStringLiteral("com.lingmo.Session"))) {
        QDBusInterface iface("com.lingmo.Session",
                             "/Session",
                             "com.lingmo.Session", bus);
        if (iface.isValid()) {
            QDBusPendingReply<> reply = iface.asyncCall(QStringLiteral("launch"), exec, args);
            reply.waitForFinished();
            if (!reply.isError())
                return true;
        }
    }

    qWarning() << "ProcessProvider: failed to launch" << exec << args;
    return false;
}
