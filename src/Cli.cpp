/*
    keepassx command line interface implementation for POSIX OSes.
    Copyleft © 2011  gluk47@gmail.com

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/


#include "Cli.h"
#include <iostream>

#define DEFINE_WHEREAMI_MACRO
#include "helpers/cli.h"
#include "Kdb3Database.h"

#include <string>
#include <SecString.h>
#include "Database.h"
#include <QtCore/qstringlist.h>

#include <readline/readline.h>
#include <readline/history.h>

#include <cerrno>
#include <cstring>
#include <ctime>

#include <termios.h>
#include <unistd.h>
#include <qmap.h>
#include <qfileinfo.h>
#include <qdir.h>

using namespace helpers;
using namespace helpers::cli;
using namespace helpers::qt;
using namespace std;

const char* Cli::version = "0.2.2";

void enable_echo(bool on = true) {
    termios settings;
    tcgetattr( STDIN_FILENO, &settings );
    settings.c_lflag = on
                    ? (settings.c_lflag |   ECHO )
                    : (settings.c_lflag & ~(ECHO));
    tcsetattr( STDIN_FILENO, TCSANOW, &settings );
}

struct tmp_echo_disable {
    tmp_echo_disable(){ enable_echo(false); }
    ~tmp_echo_disable() { enable_echo(true); cout << "\n"; }
};
const char* Qstring2constchars(const QString& _) {
    return _.toUtf8().constData();
}
char* strdup(const QString& _) {
    return strdup(Qstring2constchars(_));
}
void Cli::ClsReminder() const {
    // sometimes they say I'm too talkative...
    const size_t eloquicity = 9;
    QString hello[eloquicity] = {
        tr("Remember: ctrl+L clears screen!"),
        tr("To clear screen, you can use ctrl + L."),
        tr("BTW. If you press Ctrl+L, the screen will be cleared."),
        tr("ONLY TODAY! TRY IT NOW! Just press ctrl+L — and the password will disappear from the screen!"),
        tr("As you may remember, ctrl+L still allows you to clear screen"),
        tr("Cast Ctrl+L to banish everything from the display"),
        tr("R u sure noone behind you's watching ur password? Ctrl+L might help u."),
        tr("Consider using Ctrl+L when you no longer need the password to clear screen"),
        tr("Ctrl+L will be glad to clear screen for you anytime you wish")
    };
    cout << hello[unsigned(time(0)) % eloquicity] << "\n";
}
struct utf8 {
    QString Data;
    utf8(const QString& _) : Data(_) {}
    utf8(IGroupHandle* _) : Data(_ != NULL? _->title() : "/") {}
    utf8(const char* _) : Data(QString::fromUtf8(_)) {}
    operator const QString&() const { return Data; }
};
ostream& operator<< (ostream& _str, const utf8& _text) {
    return _str << _text.Data.toUtf8().data();
}

Cli& Cli::the() {
    static Cli interface;
    return interface;
}
bool Cli::openDatabase(const QString& filename, bool IsAuto) {
    rl_attempted_completion_function = tab_complete;
    if (!QFile::exists(filename)){
        cout << __WHEREAMI__ << "> " <<  tr("File «")
             << filename << tr("» was not found.") << endl;
//         return false;
        cout << tr("Would you like to create there a new database? ");
        if (ReadYesNoChar("", "y", "n", 'y') != 'y')
            return false;
        db.reset(new Kdb3Database(true));
        db->create();
        db->changeFile(filename);
        QString passwd = ReadPassword(PasswdConfirm::yes);
        db->setKey(passwd, "");
        db->generateMasterKey();
        QStringList initial_groups;
        initial_groups.push_back(tr("Internet", "default group title"));
        initial_groups.push_back(tr("eMail", "default group title"));
        md(initial_groups);
        db->save();
        _Created = true;
        return true;
    }
    _Created = false;
    _Lockfile = filename + ".lock";
    if (QFile::exists(_Lockfile)) {
        dbReadOnly = true;
//         char ignbuf[8];
//         cin.readsome(ignbuf, 7);
        const char readonly_ans =
            ReadYesNoChar(tr("The database you are trying to open is locked.\n"
                "This means that either someone else has opened the file or KeePassX crashed last time it opened the database.\n\n"
                "Do you want to open it anyway read only?"
        ).toStdString(),
            string("yд"),
            string("nн"),
            'y');
        if (readonly_ans != 'y') return false;
    }
//     config->setLastKeyLocation(QString());
//     config->setLastKeyType(PASSWORD);
    db.reset(new Kdb3Database(true));
    if (!dbReadOnly && !QFile::exists(_Lockfile)){
        QFile lock(_Lockfile);
        if (!lock.open(QIODevice::WriteOnly))
            dbReadOnly = true;
    }
    QString pass = ReadPassword(PasswdConfirm::no);
    if (pass.isNull()) {
        cout << tr("Failed to get the database password.\nAu revoir!") << "\n";
        return false;
    }
    db->setKey(pass, "");
    if(db->load(filename, dbReadOnly)) {
        if (IsLocked)
            resetLock();
        updateCurrentFile(filename);
        saveLastFilename(filename);
        setStateFileOpen(true);
        setStateFileModified(static_cast<Kdb3Database*>(db.get())->hasPasswordEncodingChanged());
    } else {
        if (!dbReadOnly && QFile::exists(_Lockfile))
            QFile::remove(_Lockfile);
        QString error = db->getError();
        if(error.isEmpty())error=tr("Unknown error while loading database.");
        cerr << tr("The following error occured while opening the database:\n")
             << error << "\n";
        if(db->isKeyError()){
            db.reset();
            return openDatabase(filename,IsAuto);
        } else {
            db.reset();
            return false;
        }
    }
    return true;
}
void Cli::closeDatabase() {
    if (db.get() != NULL) {
        db->close();
        db.reset();
    }
    if (!dbReadOnly && QFile::exists(_Lockfile)){
        if (!QFile::remove(_Lockfile))
            cerr << tr("Error") << "\n"
            << tr("Couldn't remove database lock file.");
    }
    _Lockfile.clear();
}
void Cli::saveLastFilename(const QString& filename) {
    return;
    ///\todo implement
    if(config->openLastFile()){
        if(config->saveRelativePaths()){
            QString Path=filename.left(filename.lastIndexOf("/"));
            Path=makePathRelative(Path,QDir::currentPath());
            config->setLastFile(Path+filename.right(filename.length()-filename.lastIndexOf("/")-1));
        }
        else
            config->setLastFile(filename);
    }
}
bool Cli::saveUnsaved() {
    if (not ModFlag)
        return true;
    if (db->groups().isEmpty()) {
        cout << tr("The database has unsaved modifications but cannot be saved since it is empty. It will be rolled back to the latest saved version.") << "\n";
    } else {
        cout << tr("The database has not been saved since last modification.\n"
                   "Would you like to save it now before exit?");
        if (helpers::cli::ReadYesNoChar("", "y", "n", 'y') == 'y') {
            if (db->save())
                cout << tr("The database has been saved.") << "\n";
            else {
                cout << tr("Failed to save the database, exit cancelled.\n"
                     "Consider using «save as», save with a new file name as a parameter.") << "\n";
                return false;
            }
        } else {
            cout << tr("The database has been closed, all unsaved modifications have been declined.")
                 << "\n";
            if (_Created)
                db->file()->remove();
        }
    }
    return true;
}
int Cli::Run(const QString& _filename) {
    std::cout << tr("Launching keepassx cli v") << version << "...\n";
    std::cout << "class Cli> openDatabase ("
              << _filename.toStdString() << ")\n";
    bool opened = openDatabase(_filename);
    std::cout << "openDatabase " << (opened? "succeed" : "fail") << "ed\n";
    if (not opened) return 1;
    QStringList cmd = readCmd();
    while (true) {
        if (not cmd.empty()
            and cmd[0] == "exit") {
            if (saveUnsaved())
                break;
        }
        else
            ProcessCmd(cmd);
        cmd = readCmd();
    }
    closeDatabase();
    return 0;
}
void Cli::search(const QStringList& _request) {
    QString pattern;
    if (_request.isEmpty()) {
        if (ReadYesNoChar("Display all records stored in the database?",
                          "y", "n", 'n') != 'y')
            return;
        pattern = "";
    } else if(_request.size() > 1) {
        cerr << "As for now, only one search request argument is supported.\n";
        if (ReadYesNoChar("Look for the first pattern in the database?", "y", "n", 'y') != 'y')
            return;
        pattern = _request[0];
    } else
        pattern = _request[0];
    QList<IEntryHandle*> SearchResults =
        db->search(NULL, pattern, false, false, true, NULL);
    for (QList<IEntryHandle*>::const_iterator i = SearchResults.constBegin();
            i != SearchResults.constEnd(); ++i) {
        IGroupHandle* group = (*i)->group();
        cerr << "\t @" << group->title() << "\n"
             << "\t     === " << (*i)->title() << " ===\n"
             << "\t    url> " << (*i)->url() << "\n"
             << "\t   user> " << (*i)->username() << "\n";
        SecString passwd = (*i)->password();
        passwd.unlock();
        cerr << "\t   pass> " << passwd.string() << "\n";
        passwd.lock();
        cerr << "\tcomment> " << (*i)->comment() << "\n"
             << "\n";
    }
    ClsReminder();
}
QStringList Cli::readCmd() {
    QStringList ret;
    const QString fname = QFileInfo(*db->file()).fileName();
    QString prompt = "[kpx@" + fname + " "
                    + (_Wd != NULL? _Wd->title(): "/") + "]";
    if(ModFlag) prompt += "*";
    if(dbReadOnly) prompt += "#";
    prompt += "$ ";
    struct auto_free {
        auto_free(char* _) : data(_){}
        ~auto_free() { free(data); }
        operator char*() const { return data; }
        char operator[](size_t _) const { return data[_]; }
        char* data;
    } line (readline(Qstring2constchars(prompt)));
    if (line == NULL) {
        if (ModFlag and db->groups().isEmpty()) {
            cout << tr("Are you sure to exit? The database is now empty and cannot be saved because of it.") << "\n";
            if (ReadYesNoChar("", "y", "n", 'y') != 'y')
                return QStringList();
        }
        cout << "Au revoir\n";
        ret.push_back("exit"); return ret;
    }
    if (line[0] == '\0') return ret;
    size_t lexem_start = 0;
    // remove leading spaces
    while (line[lexem_start] == ' ') ++lexem_start;
    // nothing but spaces in the line?
    if (line[lexem_start] == 0) return ret;
    add_history(line);
    size_t i;
    char quote = 0;
    QString lexem;
    bool char_escaped = false;
    for (i = lexem_start; line[i] != 0; ++i) {
        const char c = line[i];
        if (quote != 0) {
            if (c == quote) {
                if (i > lexem_start)
                    lexem += QString::fromUtf8(line + lexem_start,
                                               i - lexem_start);
                quote = 0;
                lexem_start = i + 1;
            }
            continue;
        } else {
            if ((c == '\'' or c == '"')
                and not
                (ret.size() > 1
                 and ret[1] != "passwd")
               ){
                quote = c;
                if (i > lexem_start + 1) {
                    lexem += QString::fromUtf8(line + lexem_start,
                                               i - lexem_start);
                }
                lexem_start = i + 1;
                continue;
            }
            if (c != ' ') continue;
            if (i > lexem_start)
                lexem += QString::fromUtf8(line + lexem_start,
                                           i - lexem_start);
            ret.push_back(lexem);
            lexem.clear();
//             ret.push_back(QString::fromUtf8(line + lexem_start, i - lexem_start));
        }
        lexem_start = i;
        while (line[lexem_start] == ' ') ++lexem_start;
        if (line[lexem_start] == 0) return ret;
        i = lexem_start - 1;
    }
    if (lexem_start < i) {
        if (quote != 0) cerr << tr("warning: autoclosing quote!") << "\n";
        lexem += QString::fromUtf8(line + lexem_start, i - lexem_start);
    }
    if (not lexem.isEmpty()) ret.push_back(lexem);
    return ret;
}
void Cli::ls(IGroupHandle* const _dir, Cli::ls_quiet::flag _quiet) const {
    QList<IGroupHandle*> group = db->groups();
    if (_dir == _Wd)
        _HSubGroups.clear();
    else if (_quiet == ls_quiet::yes) {
        cerr << "[debug] ls(_dir != _Wd, ls_quiet::yes) has no effect, check you code.\n";
        return;
    }
    for (QList<IGroupHandle*>::const_iterator i = group.constBegin();
         i != group.constEnd(); ++i) {
        if ((*i)->parent() != _dir)
            continue;
        if (_dir == _Wd) _HSubGroups.insert((*i)->title(), *i);
        if (not _quiet) cout << "+ " << (utf8)(*i)->title() << "\n";
    }

    if (_dir == NULL) return;
    QList<IEntryHandle*> entries = db->entries(_dir);
    _HEntries.clear();
    for (QList<IEntryHandle*>::const_iterator i = entries.constBegin();
         i != entries.constEnd(); ++i) {
        if (_dir == _Wd) _HEntries.insert((*i)->title(), *i);
        if (not _quiet) cout << "⋅ " << (utf8)(*i)->title() << "\n";
    }
}
void Cli::ls(const QStringList& _args) const {
    if (_args.isEmpty()) {
        ls();
        return;
    }
    for (size_t i = 0; i < _args.size(); ++i) {
        if (_args[i] == "/") {
            cerr << "=== / ===\n";
            ls(NULL);
            continue;
        }
        if (_args[i] == "..") {
            if (_Wd == NULL) {
                cerr << tr("We're in the root group, there is no parent here. What did you mean?") << "\n";
            } else {
                IGroupHandle* p = _Wd->parent();
                cout << "=== " << (utf8)p << " ===\n";
                ls(p);
            }
            continue;
        }
        QList<IGroupHandle*> found = find_in_cwd<IGroupHandle>(_args[i]);
        for (QList<IGroupHandle*>::iterator i = found.begin();
                i != found.end(); ++i) {
            cout << "=== " << (utf8)(*i) << " ===\n";
            ls(*i);
        }
    }
}
void Cli::help() const {
    cout << tr("l, ls — list entries and groups in the current group.\n")
         << tr("cd <subgroup>  — go to a subgroup of the current group.\n")
         << tr("cd .., s — go to the parent group.\n")
         << tr("cd -, back, p — go to the previous group (double «p» moves to the previous group, then back to the current one).\n")
         << tr("cd / — go to the root group.\n")
         << tr("cat <entry> — display an entry information.\n")
         << tr("passwd <entry> — show a passwd stored in the entry. Use Ctrl+L to clear screen after that.\n")
         << tr("set — modify a database entry. Type «set help» for details.\n")
         << tr("create <entry> — create new entry. Cannot be performed from the root group.\n")
         << tr("rm <entry> — completely remove an entry from the database. Cannot be undone.\n")
         << tr("md <group>, mkdir <group> — create a new subgroup here.\n")
         << tr("save — save the database to disk.\n")
         << tr("save <filename> — save the database under the new name and close the original one. The saved database remains open.\n")
         << tr("clear, Ctrl + L — clear screen.\n")
         << tr("find, search:\n"
         "\twith one parameter: look through the database for the parameter,\n"
         "\twithout parameters: display all records in the database.\n")
         << tr("help — display the help.\n")
         << tr("exit, Ctrl + D — shutdown the program. If the database has unsaved modifications, the prompt will be displayed.");
}
void Cli::cd(const QString& _subgroup) {
    if (_HSubGroups.empty()) ls(ls_quiet::yes);
    if (_subgroup == "-") {
        std::swap(_Wd, _Previous_Wd);
        _HSubGroups.clear();
        _HEntries.clear();
        return;
    }
    if (_subgroup == "..") {
        if (_Wd == NULL) return;
        _Previous_Wd = _Wd;
        _Wd = _Wd->parent();
        _HSubGroups.clear();
        _HEntries.clear();
        return;
    }
    if (_subgroup == "/") {
        _Previous_Wd = _Wd;
        _Wd = NULL;
        _HSubGroups.clear();
        _HEntries.clear();
        return;
    }
    IGroupHandle* _ = confirm_first<IGroupHandle>(_subgroup);
    if (_ == NULL) {
        cerr << tr("There is no subgroup «") << _subgroup
             << tr("» in the current group.\n");
        return;
    }

    _Previous_Wd = _Wd;
    _Wd = _;
    _HSubGroups.clear();
    _HEntries.clear();
}
QString Cli::UiString(syntax::Type<IEntryHandle>) const { return tr("record"); }
QString Cli::UiString(syntax::Type<IGroupHandle>) const { return tr("group"); }
template <typename T>
QList<T*> Cli::find_in_cwd(const QString& _entry) const {
    if (_entry.isEmpty()) return QList<T*>();
    const QMultiMap<QString,T*> lst = _Wd_map<T>();
    if (lst.empty()) ls(ls_quiet::yes);
    const QList<T*> keys = lst.values(_entry);
    if (keys.empty())
        cout << tr("There is no ")
             << UiString(syntax::Type<T>())
             << tr(" with title «") << (utf8)_entry
             << tr("» in the current group. Consider using the «l» command or try «?».\n");
    return keys;
}
void Cli::cat(const QString& _entry) const {
    const QList<IEntryHandle*> keys = find_in_cwd<IEntryHandle>(_entry);
    for (QList<IEntryHandle*>::const_iterator i = keys.constBegin();
         i != keys.constEnd(); ++i) {
        cout << "\t=== " << (utf8)(*i)->title() << " ===\n"
             << tr("\turl>      ") << (utf8)(*i)->url() << tr("\n")
             << tr("\tuser>     ") << (utf8)(*i)->username() << tr("\n")
             << tr("\tpass> <type «passwd ") << (utf8)_entry << tr("» to reveal the password>\n")
             << tr("\tcomment>  ") << (utf8)(*i)->comment() << tr("\n")
             << tr("\n");
    }
}
void Cli::passwd(const QString& _entry) const {
    const QList<IEntryHandle*> keys = find_in_cwd<IEntryHandle>(_entry);
    for (QList<IEntryHandle*>::const_iterator i = keys.constBegin();
         i != keys.constEnd(); ++i) {
        SecString passwd = (*i)->password();
        passwd.unlock();
        cout << (utf8)(*i)->title() << "> " << (utf8)passwd.string() << "\n";
        passwd.lock();
        ClsReminder();
    }
}
template<typename T>
T* Cli::confirm_first(const QString& _title) {
    QList <T*> entries = find_in_cwd<T>(_title);
    if (entries.empty()) return NULL;
    ///\todo support more than one entry with the same name in one group
    if (entries.size() > 1) {
        cout << tr("There is more than one record with the name «")
                << (utf8)_title << tr("» in the current group. ");
        if (helpers::cli::ReadYesNoChar(
            tr("The first one will be modified, is it okay?").toStdString(),
                                        "y", "n", 'y') == 'n') return NULL;
    }
    return entries[0];
}
void Cli::set(const QStringList& _params) {
    if (_params.isEmpty() or _params[0] == "help" or _params[0] == "?") {
        cout << tr("\e[1m== set help ==\e[0m\n");
        cout << tr("Hereafter the word «record» means the title of an existing record in the current group.\n"
        "The list of the records you can get either using the command «ls» or by pressing <Tab> twice after the whole previous part of the set command.\n");
        cout << tr(" \e[1m⋅\e[0m set title record new_title\n");
        cout << tr(" \e[1m⋅\e[0m set user record new_username\n");
        cout << tr(" \e[1m⋅\e[0m set passwd record new_passwd\n");
        cout << tr(" \e[1m⋅\e[0m set url record new_passwd\n");
        cout << tr(" \e[1m⋅\e[0m set comment record new_passwd\n");
        cout << tr("if the last argument (new_...) is ommitted, the existing data field will be cleared, but you cannot clear title.");
        return;
    }
    if(_params.size() < 2) {
        cerr << tr("You need to supply 3 parameters to the command «set ")
        << _params[0] << tr("»:\nset ")
        << _params[0] << tr(" title new_") << _params[0] << "\n"
        << tr("Pass \"\" as the third argument in case you wish to clear the field. It is impossible to clear the title anyway.");
        return;
    }
    IEntryHandle* _ = confirm_first<IEntryHandle>(_params[1]);
    if (_ == NULL) return;
    if (_params[0] == "comment") {
        if (_params.size() < 3)
            _->setComment(QString());
        else
            _->setComment(_params[2]);
        return;
    }
    if(_params.size() < 3) {
        cerr << tr("You need to supply 3 parameters to the command «set ")
        << _params[0] << tr("»:\nset ")
        << _params[0] << tr(" title new_") << _params[0] << tr("\n")
        << tr("Pass \"\" as the 3rd argument to clear the specified field. The title cannot be cleared.") << "\n";
        return;
    }
    if (_params[0] == "title") {
        _->setTitle(_params[2]);
        ModFlag = true;
        _HEntries.remove(_params[1], _);
        _HEntries.insert(_params[2], _);
        return;
    }
    if (_params[0] == "passwd") {
        SecString p;
        QString sp (_params[2]);
        p.setString(sp, true);
        _->setPassword(p);
        ModFlag = true;
        return;
    }
    if (_params[0] == "user"
            or _params[0] == "login"
            or _params[0] == "username") {
        _->setUsername(_params[2]);
        ModFlag = true;
        return;
    }
    if (_params[0] == "url") {
        _->setUrl(_params[2]);
        ModFlag = true;
        return;
    }
}
void Cli::create(const QStringList& _entries) {
    if (_Wd == NULL) {
        cout << tr("It is impossible to create an entry in the root group, sorry.")
             << "\n";
        return;
    }
    if (_HEntries.isEmpty()) ls(ls_quiet::yes);
    for (size_t i = 0; i < _entries.size(); ++i) {
        if (_HEntries.contains(_entries[i])) {
            cout << tr("Current group already contains an item with title «")
                 << _entries[i] << tr("». If you create a new one, you won't be able to modify one of the entries from this console interface of keepassx. ");
            if (helpers::cli::ReadYesNoChar(tr("Continue?").toStdString(), "y", "n", 'n') == 'n')
                continue;
        }
        IEntryHandle* _new = db->newEntry(_Wd);
        _new->setTitle(_entries[i]);
        ModFlag = true;
        if (not _HEntries.empty())
            _HEntries.insert(_entries[i], _new);
    }
}
void Cli::md(const QStringList& _entries) {
    for (size_t i = 0; i < _entries.size(); ++i) {
        if (_HSubGroups.contains(_entries[i])) {
            cout << tr("Current group already contains a subgroup named «")
                 << (utf8)_entries[i] << tr("». If you create a new one, you won't be able to cd to one of the groups from this console interface of keepassx. ");
            if (helpers::cli::ReadYesNoChar(tr("Continue?").toStdString(), "y", "n", 'n') == 'n')
                continue;
        }
        CGroup newGroup;
        newGroup.Title = _entries[i];
        IGroupHandle* _new = db->addGroup(&newGroup, _Wd);
        ModFlag = true;
        if (not _HSubGroups.empty())
            _HSubGroups.insert(_entries[i], _new);
    }
}
void Cli::rm(const QStringList& _entries) {
    for (size_t i = 0; i < _entries.size(); ++i) {
        IEntryHandle* const _ = confirm_first<IEntryHandle>(_entries[i]);
        if (_ == NULL) continue;
        db->deleteEntry(_);
        ModFlag = true;
        _HEntries.remove(_entries[i], _);
    }
}
void Cli::rm_rf(const QStringList& _names) {
    if (_HSubGroups.isEmpty()) ls(ls_quiet::yes);
    for (size_t i = 0; i < _names.size(); ++i) {
        IGroupHandle* _ = confirm_first<IGroupHandle>(_names[i]);
        if (_ == NULL)
            continue;
        if (_HSubGroups.size() == 1) {
            cout << tr("You are about to delete the last group in the database. You won't be able to save it until some new group is created. If you wish to save the database now, enter «n», otherwise answer «y» to continue deletion");
            if (ReadYesNoChar("", "y", "n", 'y') != 'y') return;
        }
        cout << tr("Are you absolutely sure you wish to irrecoverably erase the whole group «") << _names[i] << tr("» with all its contents? This operation cannot be undone.");
        if (ReadYesNoChar("", "y", "n", 'n') != 'y') continue;
        db->deleteGroup(_);
        if (not _HSubGroups.isEmpty())
            _HSubGroups.remove(_names[i], _);
        ModFlag = true;
    }
}
bool Cli::save(const QStringList& _filename) {
    if (_filename.size() > 1) {
        cerr << tr("The command «save» may have one parameter: a new file name. In this case it associates the opened db with the new filename and saves it. No more parameters are supported, what did you mean?\nFeature requests are welcomed at gluk47+kpx@gmail.com") << "\n";
        return false;
    }
    if (not _filename.isEmpty()) {
        QFileInfo f(helpers::qt::expandTilde(_filename[0]));
        QString target = f.absoluteFilePath();
        if (target.isEmpty()) {
            cerr << tr("Fail to resolve filename «") << _filename[0] << tr("». You should specify a filename in an existing directory without dangling symbolic links in the path. The target file may exist in which case it will be overwritten.") << "\n";
            return false;
        }
        db->changeFile(target);
        QString oldfname = db->file()->fileName();
        if (not db->save()) {
            db->changeFile(oldfname);
            cerr << tr("The database filename has not been changed: an attempt to save the db with the new name failed.\nSystem responce: ")
                 << strerror(errno) << "\n"
                 << tr("The database has not been saved at all.\n");
        } else {
            cout << tr("The database has been saved successfully.") << "\n";
            ModFlag = false;
        }
        return true;
    }
    if (_Wd == NULL) {
        if (db->groups().isEmpty()) {
            // no, that's not cli limitation, db->save() just does absolutely nothing for an empty database.
            cerr << tr("Sorry, it is impossible to save an empty database.") << "\n";
            return false;
        }
    }
    if (dbReadOnly) {
        cerr << tr("The database has been opened read only. Use save as..., I mean, save with a parameter-new filename.") << tr("\n");
        return false;
    }
    if (db->save()) {
        cout << tr("The database has been saved successfully.") << tr("\n");
        dbReadOnly = false;
        ModFlag = false;
    }
    else {
        cout << tr("Failed to save the database.\nSystem responce: ")
             << strerror(errno) << tr("\n");
    }
    return true;
}
QString Cli::ReadPassword(PasswdConfirm::flag _confirmation_needed) {
    QString pass;
   {cout << tr("Please, enter password: ");
    tmp_echo_disable __rped__;
    string s;
    cin >> s;
    pass = QString::fromUtf8(s.c_str());
   }if (not cin) return QString();
    if (_confirmation_needed == PasswdConfirm::no) return pass;

    QString confirm;
    while (confirm != pass and cin) {
        cout << tr("Reenter password or hit Ctrl+D to break: ");
        tmp_echo_disable __rped__;
        string s;
        cin >> s;
        confirm = QString::fromUtf8(s.c_str());
    }
    if (cin) return pass;
    return QString(); //isNull, not isEmpty
}
void Cli::clearScreen() {
#if 0
    int result;
    setupterm( NULL, STDOUT_FILENO, &result );
    const char* cmd;
    if (result > 0)
        cmd = tigetstr( "clear" );
    else {
        cerr << "Sorry, unable to get a command for the «clear» action from the terminal\n";
        return;
    }
    putp( console_clearscreen );
    // fails to compile, I've given it up.
#endif
// I'm sorry, but that's far easier and does not use lots of resources, though uses more than nessessary. it's not inside a loop, so there's actually not too much to worry about.
    system("clear");
}
void Cli::ProcessCmd(const QStringList& _) {
    if (_.empty()) return;
    if (_[0] == "ls" || _[0] == "l") ls(_.mid(1));
    else if (_[0] == "cd") cd(_.size() > 1 ? _[1] : "/");
    else if (_[0] == "s") cd ("..");
    else if (_[0] == "p" or _[0] == "back") cd ("-");
    else if (_[0] == "cat") {
        QStringList::const_iterator i = _.constBegin();
        while (++i != _.constEnd()) cat (*i);
    }
    else if (_[0] == "passwd") {
        QStringList::const_iterator i = _.constBegin();
        while (++i != _.constEnd()) passwd (*i);
    }
    else if (_[0] == "set") set(_.mid(1));
    else if (_[0] == "create") create(_.mid(1));
    else if (_[0] == "rm") rm(_.mid(1));
    else if (_[0] == "md" || _[0] == "mkdir") md(_.mid(1));
    else if (_[0] == "rd" || _[0] == "rmdir") rm_rf(_.mid(1));
    else if (_[0] == "save") save(_.mid(1));
    else if (_[0] == "find" || _[0] == "search") search(_.mid(1));
    else if (_[0] == "clear") clearScreen();
    else if (_[0] == "help" || _[0] == "?") help();
    else cerr << tr("The command «") << (utf8)_[0]
              << tr("» is undefined, sorry. Try «help» or «?».")
              << "\n";
}
// =================== GNU Readline interface ===================
char** Cli::tab_complete(const char* _beginning, int _start, int /*_end*/) {
    rl_attempted_completion_over = true;
    if (_start == 0) return rl_completion_matches(_beginning, cmd_completer);
    const QString user_input = QString::fromUtf8(rl_line_buffer);
    if (   user_input.startsWith("cat ")
        or user_input.startsWith("passwd ")
        or user_input.startsWith("rm ")
       ) 
        return rl_completion_matches(_beginning, entries_completer);
    if (   user_input.startsWith("cd ")
        or user_input.startsWith("rd ")
        or user_input.startsWith("rmdir ")
        or user_input.startsWith("l ")
        or user_input.startsWith("ls ")
       )
        return rl_completion_matches(_beginning, dir_completer);
    if (user_input.startsWith("set ")) {
        QString set_cmd = user_input.mid(4);
        if (not set_cmd.contains(QRegExp("[^ ]+ ")))
            // awaiting action for set
            return rl_completion_matches(_beginning, set_completer);
        if (not set_cmd.contains(QRegExp("[^ ]+ [^ ]+ ")))
            return rl_completion_matches(_beginning, entries_completer);
        return NULL; 
    }
    if (user_input.startsWith("save "))
        rl_attempted_completion_over = false;
    return NULL;
}
char* complete(const char* _beginning, const QStringList& _list, int& _start) {
    while (++_start < _list.size()) {
        if (_list[_start].startsWith((utf8)_beginning)) {
            QString found(_list[_start]);
            if (found.contains(' ')) {
                if (found.contains('\''))
                    found.replace('\'', "'\"'");
                found = '"' + found + "\"";
            }
            return strdup(found);
        }
    }
    return NULL;
}
char* Cli::entries_completer(const char* _beginning, int _state) {
    if (the()._HEntries.empty()) the().ls(ls_quiet::yes);
    static int i; if (_state == 0) i = -1;
    return complete(_beginning, the()._HEntries.keys(), i);
}
char* Cli::dir_completer(const char* _beginning, int _state) {
    if (the()._HSubGroups.empty()) the().ls(ls_quiet::yes);
    static int i; if (_state == 0) i = -1;
    return complete(_beginning, the()._HSubGroups.keys(), i);
}
QStringList Cli::_AllCmds;
char* Cli::cmd_completer(const char* _beginning, int _state) {
    static IGroupHandle* prev_wd = NULL;
    if (_AllCmds.empty() or prev_wd != the().Wd()) {
#define _(cmd) _AllCmds.push_back(cmd)
        _("cd"), _("s"), _("p");
        _("l"), _("ls"); _("mkdir"), _("rmdir");
        if (the().Wd() != NULL) {
            _("cat"), _("passwd");
            _("set"), _("create"), _("rm");
        }
        _("save");
        _("clear");
        _("find"), _("search");
        _("?"), _("help");
        _("exit");
        prev_wd = the().Wd();
#undef _
    }
    static int i; if (_state == 0) i = -1;
    return complete(_beginning, _AllCmds, i);
}
char* Cli::set_completer(const char* _beginning, int _state) {
    static int i; if (_state == 0) i = -1;
    QStringList cmds;
#define _(cmd) cmds.push_back(cmd)
    _("title"); _("passwd"); _("url"), _("comment"); _("user");
    _("help");
#undef _
    return complete(_beginning, cmds, i);
}
