/*
    Copyright (c) 2015, Lukas Holecek <hluk@email.cz>

    This file is part of CopyQ.

    CopyQ is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    CopyQ is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with CopyQ.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "platformcommon.h"
#include "platformwindow.h"

#include <QRegExp>
#include <QSettings>

bool pasteWithCtrlV(PlatformWindow &window)
{
    const QRegExp re( QSettings().value("paste_with_ctrl_v_windows").toString() );
    return !re.isEmpty() && re.indexIn(window.getTitle()) != -1;
}
