/*
    Copyright (c) 2014, Lukas Holecek <hluk@email.cz>

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

#include "scriptable.h"

#include "common/command.h"
#include "common/commandstatus.h"
#include "common/common.h"
#include "common/mimetypes.h"
#include "item/clipboarditem.h"
#include "item/serialize.h"
#include "../qt/bytearrayclass.h"
#include "../qxt/qxtglobal.h"

#include <QApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QScriptContext>
#include <QScriptEngine>
#include <QScriptValueIterator>
#ifdef HAS_TESTS
#   include <QSettings>
#endif

Q_DECLARE_METATYPE(QByteArray*)

namespace {

const char *const programName = "CopyQ Clipboard Manager";

struct CommandHelp {
    CommandHelp()
        : cmd()
        , desc()
        , args()
    {
    }

    CommandHelp(const char *command, const QString &description)
        : cmd(command)
        , desc(description)
        , args()
    {
    }

    CommandHelp &addArg(const QString &arg)
    {
        args.append(' ');
        args.append(arg);
        return *this;
    }

    QString toString() const
    {
        if (cmd.isNull())
            return "\n";

        const int indent = 23;
        bool indentFirst = desc.startsWith('\n');
        return QString("    %1").arg(cmd + args, indentFirst ? 0 : -indent)
                + (indentFirst ? QString() : QString("  "))
                + QString(desc).replace('\n', "\n" + QString(' ')
                    .repeated(4 + 2 + (indentFirst ? 0 : indent))) + "\n";
    }

    QString cmd;
    QString desc;
    QString args;
};

QList<CommandHelp> commandHelp()
{
    return QList<CommandHelp>()
        << CommandHelp("show",
                       Scriptable::tr("Show main window."))
        << CommandHelp("hide",
                       Scriptable::tr("Hide main window."))
        << CommandHelp("toggle",
                       Scriptable::tr("Show or hide main window."))
        << CommandHelp("menu",
                       Scriptable::tr("Open context menu."))
        << CommandHelp("exit",
                       Scriptable::tr("Exit server."))
        << CommandHelp("disable, enable",
                       Scriptable::tr("Disable or enable clipboard content storing."))
        << CommandHelp()
        << CommandHelp("clipboard",
                       Scriptable::tr("Print clipboard content."))
           .addArg("[" + Scriptable::tr("MIME") + "]")
    #ifdef COPYQ_WS_X11
        << CommandHelp("selection",
                       Scriptable::tr("Print X11 selection content."))
           .addArg("[" + Scriptable::tr("MIME") + "]")
    #endif
        << CommandHelp("paste",
                       Scriptable::tr("Paste clipboard to current window\n"
                                      "(may not work with some applications)."))
        << CommandHelp("copy", Scriptable::tr("Set clipboard text."))
           .addArg(Scriptable::tr("TEXT"))
        << CommandHelp("copy", Scriptable::tr("\nSet clipboard content."))
           .addArg(Scriptable::tr("MIME"))
           .addArg(Scriptable::tr("DATA"))
           .addArg("[" + Scriptable::tr("MIME") + " " + Scriptable::tr("DATA") + "]...")
        << CommandHelp()
        << CommandHelp("length, count, size",
                       Scriptable::tr("Print number of items in history."))
        << CommandHelp("select",
                       Scriptable::tr("Copy item in the row to clipboard."))
           .addArg("[" + Scriptable::tr("ROW") + "=0]")
        << CommandHelp("next",
                       Scriptable::tr("Copy next item from current tab to clipboard."))
        << CommandHelp("previous",
                       Scriptable::tr("Copy previous item from current tab to clipboard."))
        << CommandHelp("add",
                       Scriptable::tr("Add text into clipboard."))
           .addArg(Scriptable::tr("TEXT") + "...")
        << CommandHelp("insert",
                       Scriptable::tr("Insert text into given row."))
           .addArg(Scriptable::tr("ROW"))
           .addArg(Scriptable::tr("TEXT"))
        << CommandHelp("remove",
                       Scriptable::tr("Remove items in given rows."))
           .addArg("[" + Scriptable::tr("ROWS") + "=0...]")
        << CommandHelp("edit",
                       Scriptable::tr("Edit items or edit new one.\n"
                                   "Value -1 is for current text in clipboard."))
           .addArg("[" + Scriptable::tr("ROWS") + "...]")
        << CommandHelp()
        << CommandHelp("separator",
                       Scriptable::tr("Set separator for items on output."))
           .addArg(Scriptable::tr("SEPARATOR"))
        << CommandHelp("read",
                       Scriptable::tr("Print raw data of clipboard or item in row."))
           .addArg("[" + Scriptable::tr("MIME") + "|" + Scriptable::tr("ROW") + "]...")
        << CommandHelp("write", Scriptable::tr("\nWrite raw data to given row."))
           .addArg("[" + Scriptable::tr("ROW") + "=0]")
           .addArg(Scriptable::tr("MIME"))
           .addArg(Scriptable::tr("DATA"))
           .addArg("[" + Scriptable::tr("MIME") + " " + Scriptable::tr("DATA") + "]...")
        << CommandHelp()
        << CommandHelp("action",
                       Scriptable::tr("Show action dialog."))
           .addArg("[" + Scriptable::tr("ROWS") + "=0...]")
        << CommandHelp("action",
                       Scriptable::tr("\nRun PROGRAM on item text in the rows.\n"
                                   "Use %1 in PROGRAM to pass text as argument."))
           .addArg("[" + Scriptable::tr("ROWS") + "=0...]")
           .addArg("[" + Scriptable::tr("PROGRAM") + " [" + Scriptable::tr("SEPARATOR") + "=\\n]]")
        << CommandHelp("popup",
                       Scriptable::tr("\nShow tray popup message for TIME milliseconds."))
           .addArg(Scriptable::tr("TITLE"))
           .addArg(Scriptable::tr("MESSAGE"))
           .addArg("[" + Scriptable::tr("TIME") + "=8000]")
        << CommandHelp()
        << CommandHelp("tab",
                       Scriptable::tr("List available tab names."))
        << CommandHelp("tab",
                       Scriptable::tr("Run command on tab with given name.\n"
                                   "Tab is created if it doesn't exist.\n"
                                   "Default is the first tab."))
           .addArg(Scriptable::tr("NAME"))
           .addArg("[" + Scriptable::tr("COMMAND") + "]")
        << CommandHelp("removetab",
                       Scriptable::tr("Remove tab."))
           .addArg(Scriptable::tr("NAME"))
        << CommandHelp("renametab",
                       Scriptable::tr("Rename tab."))
           .addArg(Scriptable::tr("NAME"))
           .addArg(Scriptable::tr("NEW_NAME"))
        << CommandHelp()
        << CommandHelp("exporttab",
                       Scriptable::tr("Export items to file."))
           .addArg(Scriptable::tr("FILE_NAME"))
        << CommandHelp("importtab",
                       Scriptable::tr("Import items from file."))
           .addArg(Scriptable::tr("FILE_NAME"))
        << CommandHelp()
        << CommandHelp("config",
                       Scriptable::tr("List all options."))
        << CommandHelp("config",
                       Scriptable::tr("Get option value."))
           .addArg(Scriptable::tr("OPTION"))
        << CommandHelp("config",
                       Scriptable::tr("Set option value."))
           .addArg(Scriptable::tr("OPTION"))
           .addArg(Scriptable::tr("VALUE"))
        << CommandHelp()
        << CommandHelp("eval, -e",
                       Scriptable::tr("\nEvaluate ECMAScript program.\n"
                                      "Arguments are accessible using with \"arguments(0..N)\"."))
           .addArg("[" + Scriptable::tr("SCRIPT") + "]")
           .addArg("[" + Scriptable::tr("ARGUMENTS") + "]...")
        << CommandHelp("session, -s, --session",
                       Scriptable::tr("\nStarts or connects to application instance with given session name."))
           .addArg(Scriptable::tr("SESSION"))
        << CommandHelp("help, -h, --help",
                       Scriptable::tr("\nPrint help for COMMAND or all commands."))
           .addArg("[" + Scriptable::tr("COMMAND") + "]...")
        << CommandHelp("version, -v, --version",
                       Scriptable::tr("\nPrint version of program and libraries."))
#ifdef HAS_TESTS
        << CommandHelp("tests, --tests",
                       QString("Run tests (append --help argument for more info)."))
        << CommandHelp("keys",
                       QString("Pass keys to the main window (used in tests)."))
           .addArg(QString("KEYS") + "...")
#endif
           ;
}

QString helpHead()
{
    return Scriptable::tr("Usage: copyq [%1]").arg(Scriptable::tr("COMMAND")) + "\n\n"
        + Scriptable::tr("Starts server if no command is specified.") + "\n"
        + Scriptable::tr("  COMMANDs:");
}

QString helpTail()
{
    return Scriptable::tr("NOTES:") + "\n"
        + Scriptable::tr("  - Use dash argument (-) to read data from stdandard input.") + "\n"
        + Scriptable::tr("  - Use double-dash argument (--) to read all following arguments without\n"
                      "    expanding escape sequences (i.e. \\n, \\t and others).") + "\n"
        + Scriptable::tr("  - Use ? for MIME to print available MIME types (default is \"text/plain\").");
}

QString argumentError()
{
    return Scriptable::tr("Invalid number of arguments!");
}

QScriptValue getValue(QScriptEngine *eng, const QString &variableName)
{
    return eng->globalObject().property(variableName);
}

template <typename T>
T getValue(QScriptEngine *eng, const QString &variableName, T defaultValue)
{
    QVariant val = getValue(eng, variableName).toVariant();
    if ( val.canConvert<T>() )
        return val.value<T>();
    else
        return defaultValue;
}

bool clipboardEquals(const QVariantMap &data, ScriptableProxy *proxy)
{
    foreach ( const QString &format, data.keys() ) {
        if ( data.value(format).toByteArray() != proxy->getClipboardData(format) )
            return false;
    }

    return true;
}

void waitFor(qint64 milliseconds)
{
    QElapsedTimer t;
    t.start();
    while ( !t.hasExpired(milliseconds) )
        QApplication::processEvents( QEventLoop::WaitForMoreEvents, milliseconds - t.elapsed() );
}

class ClipboardBrowserRemoteLock
{
public:
    ClipboardBrowserRemoteLock(ScriptableProxy *proxy, int rows)
        : m_proxy(rows > 4 ? proxy : NULL)
    {
        if (m_proxy != NULL)
            m_proxy->browserLock();
    }

    ~ClipboardBrowserRemoteLock()
    {
        if (m_proxy != NULL)
            m_proxy->browserUnlock();
    }

private:
    ScriptableProxy *m_proxy;
};

} // namespace

Scriptable::Scriptable(ScriptableProxy *proxy, QObject *parent)
    : QObject(parent)
    , QScriptable()
    , m_proxy(proxy)
    , m_engine(NULL)
    , m_baClass(NULL)
    , m_inputSeparator("\n")
    , m_currentPath()
    , m_actionId()
    , m_input()
{
}

void Scriptable::initEngine(QScriptEngine *eng, const QString &currentPath, const QByteArray &actionId)
{
    m_engine = eng;
    QScriptEngine::QObjectWrapOptions opts =
              QScriptEngine::ExcludeChildObjects
            | QScriptEngine::SkipMethodsInEnumeration
            | QScriptEngine::ExcludeSuperClassMethods
            | QScriptEngine::ExcludeSuperClassProperties
            | QScriptEngine::ExcludeSuperClassContents
            | QScriptEngine::ExcludeDeleteLater;

    QScriptValue obj = eng->newQObject(this, QScriptEngine::QtOwnership, opts);

    // Keep internal functions as parseInt() or encodeURIComponent().
    QScriptValue oldObj = eng->globalObject();
    QScriptValueIterator it(oldObj);
    while (it.hasNext()) {
        it.next();
        obj.setProperty(it.name(), it.value(), it.flags());
    }

    eng->setGlobalObject(obj);
    eng->setProcessEventsInterval(1000);

    m_baClass = new ByteArrayClass(eng);
    obj.setProperty( "ByteArray", m_baClass->constructor() );

    setCurrentPath(currentPath);
    m_actionId = actionId;
}

QScriptValue Scriptable::newByteArray(const QByteArray &bytes)
{
    return m_baClass->newInstance(bytes);
}

QByteArray Scriptable::fromString(const QString &value) const
{
  QByteArray bytes = value.toUtf8();
#ifdef COPYQ_OS_WIN
  bytes.replace('\n', "\r\n");
#endif
  return bytes;
}

QString Scriptable::toString(const QScriptValue &value) const
{
    QByteArray *bytes = toByteArray(value);
    return (bytes == NULL) ? value.toString()
                           : QString::fromUtf8( bytes->data() );
}

bool Scriptable::toInt(const QScriptValue &value, int &number) const
{
    bool ok;
    number = toString(value).toInt(&ok);
    if (!ok)
        return false;

    return true;
}

QByteArray *Scriptable::toByteArray(const QScriptValue &value) const
{
    if (value.scriptClass() == m_baClass)
        return qscriptvalue_cast<QByteArray*>(value.data());
    else
        return NULL;
}

bool Scriptable::toItemData(const QScriptValue &value, const QString &mime, QVariantMap *data) const
{
    if (mime == mimeItems) {
        const QByteArray *itemData = toByteArray(value);
        if (!itemData)
            return false;

        return deserializeData(data, *itemData);
    }

    if (!mime.startsWith("text/") && value.scriptClass() == m_baClass)
        data->insert( mime, *toByteArray(value) );
    else
        data->insert( mime, toString(value).toUtf8() );

    return true;
}

QScriptValue Scriptable::applyRest(int first)
{
    if ( first >= context()->argumentCount() )
        return QScriptValue();

    QScriptValue fn = context()->argument(first);
    QString name = toString(fn);
    fn = getValue(engine(), name);
    if ( !fn.isFunction() ) {
        throwError( tr("Name \"%1\" doesn't refer to a function.").arg(name).toUtf8() );
        return QScriptValue();
    }

    QScriptValueList args;
    for (int i = first + 1; i < context()->argumentCount(); ++i)
        args.append( context()->argument(i) );

    return fn.call(QScriptValue(), args);
}

const QString &Scriptable::getInputSeparator() const
{
    return m_inputSeparator;
}

void Scriptable::setInputSeparator(const QString &separator)
{
    m_inputSeparator = separator;
}

const QString &Scriptable::getCurrentPath() const
{
    return m_currentPath;
}

void Scriptable::setCurrentPath(const QString &path)
{
    m_currentPath = path;
}

QString Scriptable::getFileName(const QString &fileName) const
{
    return QDir::isRelativePath(fileName) ? getCurrentPath() + '/' + fileName
                                          : fileName;
}

QString Scriptable::arg(int i, const QString &defaultValue)
{
    return i < argumentCount() ? toString(argument(i)) : defaultValue;
}

void Scriptable::throwError(const QString &errorMessage)
{
    context()->throwError( fromString(errorMessage + '\n') );
}

void Scriptable::sendMessageToClient(const QByteArray &message, int exitCode)
{
    emit sendMessage(message, exitCode);
}

QScriptValue Scriptable::version()
{
    return tr(programName) + " v" COPYQ_VERSION " (hluk@email.cz)\n"
            + tr("Built with: ")
            + "Qt " + QT_VERSION_STR +
            + ", LibQxt " + QXT_VERSION_STR
            + '\n';
}

QScriptValue Scriptable::help()
{
    const QString &cmd = arg(0);
    QString helpString;

    if (cmd.isNull())
        helpString.append(helpHead() + "\n");

    bool found = cmd.isNull();
    foreach (CommandHelp hlp, commandHelp()) {
        if (cmd.isNull()) {
            helpString.append(hlp.toString());
        } else if (hlp.cmd.contains(cmd)) {
            found = true;
            helpString.append(hlp.toString());
        }
    }

    if (!found) {
        throwError( tr("Command not found!") );
        return QString();
    }

    if ( cmd.isNull() ) {
        helpString.append("\n" + helpTail() + "\n\n" + tr(programName)
            + " v" + COPYQ_VERSION + " (hluk@email.cz)\n");
    }

    return helpString;
}

void Scriptable::show()
{
    if (argumentCount() == 0) {
        m_proxy->showWindow();
    } else if (argumentCount() == 1) {
        m_proxy->showBrowser(toString(argument(0)));
    } else {
        throwError(argumentError());
    }
}

void Scriptable::hide()
{
    m_proxy->close();
}

void Scriptable::toggle()
{
    m_proxy->toggleVisible();
}

void Scriptable::menu()
{
    if (argumentCount() == 0) {
        m_proxy->toggleMenu();
    } else if (argumentCount() == 1) {
        m_proxy->toggleMenu(toString(argument(0)));
    } else {
        throwError(argumentError());
    }
}

void Scriptable::exit()
{
    QByteArray message = fromString( tr("Terminating server.\n") );
    sendMessageToClient(message, CommandFinished);
    emit requestApplicationQuit();
}

void Scriptable::disable()
{
    m_proxy->disableMonitoring(true);
}

void Scriptable::enable()
{
    m_proxy->disableMonitoring(false);
}

QScriptValue Scriptable::monitoring()
{
    return m_proxy->isMonitoringEnabled();
}

void Scriptable::ignore()
{
    m_proxy->ignoreCurrentClipboard();
}

QScriptValue Scriptable::clipboard()
{
    const QString &mime = arg(0, mimeText);
    return newByteArray( m_proxy->getClipboardData(mime) );
}

QScriptValue Scriptable::selection()
{
#ifdef COPYQ_WS_X11
    const QString &mime = arg(0, mimeText);
    return newByteArray( m_proxy->getClipboardData(mime, QClipboard::Selection) );
#else
    return QScriptValue();
#endif
}

void Scriptable::copy()
{
    int args = argumentCount();
    QVariantMap data;

    if (args == 1) {
        QScriptValue value = argument(0);
        data[mimeText] = toString(value).toUtf8();
    } else if (args % 2 == 0) {
        for (int i = 0; i < args; ++i) {
            // MIME
            QString mime = toString(argument(i));

            // DATA
            toItemData(argument(++i), mime, &data);
        }
    } else {
        throwError(argumentError());
        return;
    }

    m_proxy->setClipboard(data);

    // Wait for clipboard to be set.
    for (int i = 0; i < 10; ++i) {
        waitFor(250);
        if ( clipboardEquals(data, m_proxy) )
            return;
    }

    throwError( tr("Failed to set clipboard!") );
}

void Scriptable::paste()
{
    m_proxy->pasteToCurrentWindow();
}

QScriptValue Scriptable::tab()
{
    const QString &name = arg(0);
    if ( name.isNull() ) {
        QString response;
        foreach ( const QString &tabName, m_proxy->tabs() )
            response.append(tabName + '\n');
        return response;
    } else {
        m_proxy->setCurrentTab(name);
        return applyRest(1);
    }
}

void Scriptable::removetab()
{
    const QString &name = arg(0);
    const QString error = m_proxy->removeTab(name);
    if ( !error.isEmpty() )
        throwError(error);
}

void Scriptable::renametab()
{
    const QString &name = arg(0);
    const QString &newName = arg(1);
    const QString error = m_proxy->renameTab(newName, name);
    if ( !error.isEmpty() )
        throwError(error);
}

QScriptValue Scriptable::length()
{
    return m_proxy->browserLength();
}

void Scriptable::select()
{
    QScriptValue value = argument(0);
    int row;
    if ( toInt(value, row) ) {
        m_proxy->browserMoveToClipboard(row);
        m_proxy->browserDelayedSaveItems();
    }
}

void Scriptable::next()
{
    m_proxy->browserCopyNextItemToClipboard();
}

void Scriptable::previous()
{
    m_proxy->browserCopyPreviousItemToClipboard();
}

void Scriptable::add()
{
    const int count = argumentCount();
    ClipboardBrowserRemoteLock lock(m_proxy, count);
    for (int i = 0; i < count; ++i) {
        QScriptValue value = argument(i);
        m_proxy->browserAdd( toString(value) );
    }

    m_proxy->browserDelayedSaveItems();
}

void Scriptable::insert()
{
    int row;
    if ( !toInt(argument(0), row) ) {
        throwError(argumentError());
        return;
    }

    QScriptValue value = argument(1);
    QVariantMap data;
    data.insert( mimeText, toString(value).toUtf8() );
    m_proxy->browserAdd(data, row);

    m_proxy->browserDelayedSaveItems();
}

void Scriptable::remove()
{
    QList<int> rows = getRows();

    if (rows.size() != argumentCount()) {
        throwError(argumentError());
        return;
    }

    if ( rows.empty() )
        rows.append(0);

    qSort( rows.begin(), rows.end(), qGreater<int>() );

    ClipboardBrowserRemoteLock lock(m_proxy, rows.size());
    foreach (int row, rows)
        m_proxy->browserRemoveRow(row);

    m_proxy->browserDelayedSaveItems();
}

void Scriptable::edit()
{
    QScriptValue value;
    QString text;
    int row;

    const int len = argumentCount();
    for ( int i = 0; i < len; ++i ) {
        value = argument(i);
        if (i > 0)
            text.append( getInputSeparator() );
        if ( toInt(value, row) ) {
            const QByteArray bytes = row >= 0 ? m_proxy->browserItemData(row, mimeText)
                                              : m_proxy->getClipboardData(mimeText);
            text.append( QString::fromUtf8(bytes) );
        } else {
            text.append( toString(value) );
        }
    }

    if ( !m_proxy->browserOpenEditor(fromString(text)) ) {
        m_proxy->showBrowser();
        if (len == 1 && row >= 0) {
            m_proxy->browserSetCurrent(row);
            m_proxy->browserEditRow(row);
        } else {
            m_proxy->browserEditNew(text);
        }
    }
}

QScriptValue Scriptable::read()
{
    QByteArray result;
    QString mime(mimeText);
    QScriptValue value;
    QString sep = getInputSeparator();

    bool used = false;
    for ( int i = 0; i < argumentCount(); ++i ) {
        value = argument(i);
        int row;
        if ( toInt(value, row) ) {
            if (used)
                result.append(sep);
            used = true;
            result.append( row >= 0 ? m_proxy->browserItemData(row, mime)
                                    : m_proxy->getClipboardData(mime) );
        } else {
            mime = toString(value);
        }
    }

    if (!used)
        result.append( m_proxy->getClipboardData(mime) );

    return newByteArray(result);
}

void Scriptable::write()
{
    int row;
    int args = argumentCount();
    int i;

    // [ROW]
    if ( toInt(argument(0), row) ) {
        if (args < 3 || args % 2 != 1 ) {
            throwError(argumentError());
            return;
        }
        i = 1;
    } else {
        if (args < 2 || args % 2 != 0 ) {
            throwError(argumentError());
            return;
        }
        row = 0;
        i = 0;
    }

    QVariantMap data;

    for (; i < args; i += 2) {
        // MIME
        const QString mime = toString(argument(i));
        // DATA
        toItemData( argument(i + 1), mime, &data );
    }

    m_proxy->browserAdd(data, row);
}

QScriptValue Scriptable::separator()
{
    setInputSeparator( toString(argument(0)) );
    return applyRest(1);
}

void Scriptable::action()
{
    QString text;
    bool anyRows = false;
    int i;
    QScriptValue value;
    QString sep = getInputSeparator();

    for ( i = 0; i < argumentCount(); ++i ) {
        value = argument(i);
        int row;
        if (!toInt(value, row))
            break;
        if (anyRows)
            text.append(sep);
        else
            anyRows = true;
        text.append( QString::fromUtf8(m_proxy->browserItemData(row, mimeText)) );
    }

    if (!anyRows) {
        text = QString::fromUtf8( m_proxy->getClipboardData(mimeText) );
    }

    const QVariantMap data = createDataMap(mimeText, text);

    if (i < argumentCount()) {
        Command command;
        command.cmd = toString(value);
        command.output = mimeText;
        command.input = mimeText;
        command.wait = false;
        command.outputTab = m_proxy->currentTab();
        command.sep = ((i + 1) < argumentCount()) ? toString( argument(i + 1) )
                                                  : QString('\n');
        m_proxy->action(data, command);
    } else {
        m_proxy->openActionDialog(data);
    }
}

void Scriptable::popup()
{
    QString title = arg(0);
    QString message = arg(1);
    int msec;
    if ( !toInt(argument(2), msec) )
        msec = 8000;
    m_proxy->showMessage(title, message, QSystemTrayIcon::Information, msec);
}

void Scriptable::exporttab()
{
    const QString &fileName = arg(0);
    if ( fileName.isNull() ) {
        throwError(argumentError());
    } else if ( !m_proxy->saveTab(getFileName(fileName)) ) {
        throwError( tr("Cannot save to file \"%1\"!").arg(fileName) );
    }
}

void Scriptable::importtab()
{
    const QString &fileName = arg(0);
    if ( fileName.isNull() ) {
        throwError(argumentError());
    } else if ( !m_proxy->loadTab(getFileName(fileName)) ) {
        throwError(
            tr("Cannot import file \"%1\"!").arg(fileName) );
    }
}

QScriptValue Scriptable::config()
{
    const QString name = arg(0);
    const QString value = arg(1);

    const QVariant result = m_proxy->config(name, value);

    if ( !result.isValid() )
        throwError( tr("Invalid option \"%1\"!").arg(name) );

    const QString output = result.toString();
    return output.isEmpty() ? QScriptValue() : output;
}

void Scriptable::eval()
{
    const QString script = arg(0);
    engine()->evaluate(script);
}

void Scriptable::currentpath()
{
    const QString path = arg(0);
    setCurrentPath(path);
}

QScriptValue Scriptable::str(const QScriptValue &value)
{
    return toString(value);
}

QScriptValue Scriptable::input()
{
    if ( !toByteArray(m_input) ) {
        sendMessageToClient(QByteArray(), CommandReadInput);
        while ( !toByteArray(m_input) )
            QApplication::processEvents();
    }

    return m_input;
}

QScriptValue Scriptable::data(const QScriptValue &value)
{
    return newByteArray( m_proxy->getActionData(m_actionId, toString(value)) );
}

void Scriptable::print(const QScriptValue &value)
{
    QByteArray *message = toByteArray(value);
    QByteArray bytes = (message != NULL) ? *message : fromString(value.toString());
    sendMessageToClient(bytes, CommandSuccess);
}

void Scriptable::abort()
{
    QScriptEngine *eng = engine();
    if (eng == NULL)
        eng = m_engine;
    if ( eng && eng->isEvaluating() ) {
        setInput(QByteArray()); // stop waiting for input
        eng->abortEvaluation();
    }
}

void Scriptable::keys()
{
#ifdef HAS_TESTS
    for (int i = 0; i < argumentCount(); ++i) {
        const QString keys = toString(argument(i));

        waitFor(500);
        const QString error = m_proxy->sendKeys(keys);
        if ( !error.isEmpty() ) {
            throwError(error);
            return;
        }

        // Make sure all keys are send (shortcuts are postponed because they can be blocked by modal windows).
        m_proxy->sendKeys("FLUSH_KEYS");
    }
#endif
}

QScriptValue Scriptable::selectitems()
{
    QList<int> rows = getRows();

    if (rows.size() != argumentCount()) {
        throwError(argumentError());
        return false;
    }

    return m_proxy->selectItems(rows);
}

QScriptValue Scriptable::selected()
{
    return m_proxy->selected();
}

QScriptValue Scriptable::selectedtab()
{
    return m_proxy->selectedTab();
}

QScriptValue Scriptable::selecteditems()
{
    return m_proxy->selectedItems();
}

QScriptValue Scriptable::index()
{
    return m_proxy->index();
}

QScriptValue Scriptable::escapeHTML()
{
    return escapeHtml(toString(argument(0)));
}

QScriptValue Scriptable::unpack()
{
    QVariantMap data;
    QScriptValue value = m_engine->newObject();

    if ( !toItemData(argument(0), mimeItems, &data) ) {
        throwError(argumentError());
        return QScriptValue();
    }

    foreach (const QString &format, data.keys())
        value.setProperty(format, newByteArray(data[format].toByteArray()));

    return value;
}

void Scriptable::setInput(const QByteArray &bytes)
{
    m_input = newByteArray(bytes);
}

QList<int> Scriptable::getRows() const
{
    QList<int> rows;

    for ( int i = 0; i < argumentCount(); ++i ) {
        int row;
        if ( toInt(argument(i), row) )
            rows.append(row);
    }

    return rows;
}
