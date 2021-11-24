/*
    This file is part of Element
    Copyright (C) 2019  Kushview, LLC.  All rights reserved.

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
*/

#pragma once

#include "controllers/Controller.h"
#include "session/CommandManager.h"
#include "RunMode.h"

namespace Element {

class Globals;
class Settings;
class UnlockStatus;

struct AppMessage;

class AppController :  public Controller,
                       public MessageListener,
                       protected ApplicationCommandTarget
{
public:
    AppController (Globals&, RunMode mode = RunMode::Standalone);
    ~AppController();

    /** Returns the running mode of this instance */
    RunMode getRunMode() const { return runMode; }

    /** Returns the CommandManager */
    inline CommandManager& getCommandManager() { return commands; }

    /** Access to global data */
    inline Globals& getWorld() { return getGlobals(); }

    /** Alias of getWorld() */
    inline Globals& getGlobals() { return world; }

    /** Returns the undo manager */
    inline UndoManager& getUndoManager() { return undo; }

    /** Child controllers should use this when files are opened and need
        to be saved in recent files.
    */
    inline void addRecentFile (const File& file) { recentFiles.addFile (file); }

    /** Activate this and children */
    void activate() override;

    /** Deactivate this and children */
    void deactivate() override;

    /** Sub controllers of the main app should inherrit this */
    class Child : public Controller,
                  protected ApplicationCommandTarget
    {
    public:
        Child() { }
        virtual ~Child() { }
        
        inline AppController& getAppController() const
        {
            auto* const app = dynamic_cast<AppController*> (getRoot());
            jassert (app); // if you hit this then you're probably calling 
                           // this before controller initialization
            return *app;
        }

        Settings& getSettings();
        Globals& getWorld();
        RunMode getRunMode() const { return getAppController().getRunMode(); }

    protected:
        friend class AppController;
        virtual bool handleMessage (const AppMessage&) { return false; }
    
        friend class ApplicationCommandTarget;
        virtual ApplicationCommandTarget* getNextCommandTarget() override { return nullptr; }
        virtual void getAllCommands (Array<CommandID>&) override {}
        virtual void getCommandInfo (CommandID, ApplicationCommandInfo&) override {}
        virtual bool perform (const InvocationInfo&) override { return false; }
    };
    
    RecentlyOpenedFilesList& getRecentlyOpenedFilesList() { return recentFiles; }
    
    void checkForegroundStatus();
    
    void run();
    
    void quit() { quitFlag.store (1); }
    bool shouldQuit() const noexcept { return quitFlag.load() > 0; };
    void resetQuitFlag() { quitFlag.store (0); }
protected:
    friend class ApplicationCommandTarget;
    ApplicationCommandTarget* getNextCommandTarget() override;
    void getAllCommands (Array<CommandID>& commands) override;
    void getCommandInfo (CommandID commandID, ApplicationCommandInfo& result) override;
    bool perform (const InvocationInfo& info) override;
    
    void handleMessage (const Message&) override;
    
private:
    friend class Application;
    File lastSavedFile;
    File lastExportedGraph;
    Globals& world;
    CommandManager commands;
    RecentlyOpenedFilesList recentFiles;
    UndoManager undo;
    RunMode runMode;
    std::atomic<int> quitFlag { 0 };
    
};

}
