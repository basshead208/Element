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

#include "controllers/AppController.h"
#include "controllers/GuiController.h"
#include "controllers/SessionController.h"
#include "controllers/WorkspacesController.h"

#include "engine/AudioEngine.h"

#include "gui/AboutComponent.h"
#include "gui/ContentComponent.h"
#include "gui/GuiCommon.h"
#include "gui/MainWindow.h"
#include "gui/PluginWindow.h"
#include "gui/SystemTray.h"
#include "gui/views/VirtualKeyboardView.h"

#include "CapsLock.h"
#include "Version.h"

namespace Element {

struct GlobalLookAndFeel
{
    GlobalLookAndFeel()     { LookAndFeel::setDefaultLookAndFeel (&look); }
    ~GlobalLookAndFeel()    { LookAndFeel::setDefaultLookAndFeel (nullptr); }
    Element::LookAndFeel look;
};

struct GuiController::KeyPressManager : public KeyListener
{
    KeyPressManager (GuiController& g) : owner (g) { }
    ~KeyPressManager() { }

    bool keyPressed (const KeyPress& key, Component* component) override
    {
        bool handled = false;
        if (isCapsLockOn() && isVirtualKeyboardVisible())
            handled = handleVirtualKeyboardPressed (key, component);
        return handled;
    }

    bool keyStateChanged (bool isKeyDown, Component* component) override
    {
        bool handled = false;
        if (isCapsLockOn() && isVirtualKeyboardVisible())
            handled = handleVirtualKeyboardStateChange (isKeyDown, component);
        return handled;
    }

private:
    bool isVirtualKeyboardVisible() const
    {
        if (auto* cc = owner.getContentComponent())
            return cc->isVirtualKeyboardVisible();
        return false;
    }

    VirtualKeyboardView* getVirtualKeyboardView() const
    {
        if (auto* cc = owner.getContentComponent())
            return cc->getVirtualKeyboardView();
        return nullptr;
    }

    bool handleVirtualKeyboardPressed (const KeyPress& key, Component* component)
    {
        if (auto* vcv = getVirtualKeyboardView())
            return vcv->keyPressed (key, component);
        return false;
    }

    bool handleVirtualKeyboardStateChange (bool isKeyDown, Component*)
    {
        if (auto* vcv = getVirtualKeyboardView())
            return vcv->keyStateChanged (isKeyDown);
        return false;
    }

    GuiController& owner;
};

static std::unique_ptr<GlobalLookAndFeel> sGlobalLookAndFeel;
static Array<GuiController*> sGuiControllerInstances;

GuiController::GuiController (Globals& w, AppController& a)
    : AppController::Child(),
      controller(a), world(w),
      windowManager (nullptr),
      mainWindow (nullptr)
{
    keys.reset (new KeyPressManager (*this));
    if (sGuiControllerInstances.size() <= 0)
        sGlobalLookAndFeel.reset (new GlobalLookAndFeel());
    sGuiControllerInstances.add (this);
    windowManager.reset (new WindowManager (*this));
}

GuiController::~GuiController()
{
    sGuiControllerInstances.removeFirstMatchingValue (this);
    if (sGuiControllerInstances.size() <= 0)
        sGlobalLookAndFeel = nullptr;
}

Element::LookAndFeel& GuiController::getLookAndFeel()
{ 
    jassert (sGlobalLookAndFeel);
    return sGlobalLookAndFeel->look;
}

void GuiController::saveProperties (PropertiesFile* props)
{
    jassert(props);
    
    if (mainWindow)
    {
        props->setValue ("mainWindowState", mainWindow->getWindowStateAsString());
        props->setValue ("mainWindowFullScreen", mainWindow->isFullScreen());
        props->setValue ("mainWindowVisible", mainWindow->isOnDesktop() && mainWindow->isVisible());
    }

    if (content)
    {
        props->setValue ("lastContentView", content->getMainViewName());
        props->setValue ("navSize",         content->getNavSize());
        props->setValue ("virtualKeyboard", content->isVirtualKeyboardVisible());
        props->setValue ("channelStrip",    content->isNodeChannelStripVisible());
        props->setValue ("accessoryView",   content->showAccessoryView());
        content->saveState (props);
    }
}

void GuiController::activate()
{
    getWorld().getDeviceManager().addChangeListener (this);
    Controller::activate();
}

void GuiController::deactivate()
{
    getWorld().getDeviceManager().removeChangeListener (this);
    nodeSelected.disconnect_all_slots();

    auto& settings = getSettings();
    saveProperties (settings.getUserSettings());
    
    closeAllPluginWindows (true);
    SystemTray::setEnabled (false);

    if (mainWindow)
    {
        if (keys)
            mainWindow->removeKeyListener (keys.get());
        
        closeAllWindows();
        mainWindow->setVisible (false);
        mainWindow->removeFromDesktop();
        mainWindow = nullptr;
    }

    keys = nullptr;

    if (windowManager)
    {
        windowManager = nullptr;
    }

    if (content)
    {
        content = nullptr;
    }

    Controller::deactivate();
}

void GuiController::closeAllWindows()
{
    if (! windowManager)
        return;
    windowManager->closeAll();
}

CommandManager& GuiController::commander() { return world.getCommandManager(); }

void GuiController::runDialog (const String& uri)
{
    if (uri == ELEMENT_PREFERENCES)
    {
        if (auto* const dialog = windowManager->findDialogByName ("Preferences"))
        {
            if (!dialog->isOnDesktop() || !dialog->isVisible())
            {
                dialog->setVisible (true);
                dialog->addToDesktop();
            }
            dialog->toFront (true);
            return;
        }

        DialogOptions opts;
        opts.content.set (new PreferencesComponent (world, *this), true);
        opts.useNativeTitleBar = true;
        opts.dialogTitle = "Preferences";
        opts.componentToCentreAround = (Component*) mainWindow.get();
        
        if (DialogWindow* dw = opts.create())
        {
            dw->setName ("Preferences");
            dw->setComponentID ("PreferencesDialog");
            windowManager->push (dw, true);        
        }
    }
}

void GuiController::closePluginWindow (PluginWindow* w) { if (windowManager) windowManager->closePluginWindow (w); }
void GuiController::closePluginWindowsFor (uint32 nodeId, const bool visible) { if (windowManager) windowManager->closeOpenPluginWindowsFor (nodeId, visible); }
void GuiController::closeAllPluginWindows (const bool visible) { if (windowManager) windowManager->closeAllPluginWindows (visible); }

void GuiController::closePluginWindowsFor (const Node& node, const bool visible)
{
    if (! node.isGraph() && windowManager)
        windowManager->closeOpenPluginWindowsFor (node, visible);
}

void GuiController::runDialog (Component* c, const String& title)
{
    DialogOptions opts;
    opts.content.set (c, true);
    opts.dialogTitle = title.isNotEmpty() ? title : c->getName();
    opts.componentToCentreAround = (Component*) mainWindow.get();
    if (windowManager)
        if (DialogWindow* dw = opts.create())
            windowManager->push (dw);
}

void GuiController::showSplash() { }

ContentComponent* GuiController::getContentComponent()
{
    if (! content)
    {
        content.reset (ContentComponent::create (controller));
        content->setSize (760, 480);
    }
    
    return content.get();
}

int GuiController::getNumPluginWindows() const
{
    return (nullptr != windowManager) ? windowManager->getNumPluginWindows()
                                      : 0;
}

PluginWindow* GuiController::getPluginWindow (const int window) const
{
    return (nullptr != windowManager) ? windowManager->getPluginWindow (window)
                                      : nullptr;
}

PluginWindow* GuiController::getPluginWindow (const Node& node) const
{
    for (int i = 0; i < getNumPluginWindows(); ++i)
        if (auto* const window = getPluginWindow (i))
            if (window->getNode() == node)
                return window;
    return nullptr;
}

void GuiController::showPluginWindowsFor (const Node& node, const bool recursive,
                                          const bool force, const bool focus)
{
    if (! node.isGraph())
    {
        if (force || (bool) node.getProperty ("windowVisible", false))
            presentPluginWindow (node, force);
        return;
    }

    if (node.isGraph() && recursive)
        for (int i = 0; i < node.getNumNodes(); ++i)
            showPluginWindowsFor (node.getNode (i), recursive, force, focus);
}

void GuiController::presentPluginWindow (const Node& node, const bool focus)
{
    if (! windowManager)
        return;

    if (node.isIONode() || node.isGraph())
    {
        DBG("[EL] not showing pugin window for: " << node.getName());
        return;
    }
    
    auto* window = windowManager->getPluginWindowFor (node);
    if (! window)
        window = windowManager->createPluginWindowFor (node);

    if (window != nullptr)
    {
        window->setName (String());

        if (getRunMode() == RunMode::Plugin)
        {
            // This makes plugin window handling more like the standalone
            // we don't want to modify the existing standalone behavior
            window->setAlwaysOnTop (true);
        }

        window->setVisible (true);
        window->toFront (focus);
    }
}

bool GuiController::haveActiveWindows() const
{
    if (mainWindow && mainWindow->isActiveWindow())
        return true;
    for (int i = 0; i < getNumPluginWindows(); ++i)
        if (getPluginWindow(i)->isActiveWindow())
            return true;
    return false;
}

void GuiController::run()
{
    auto& settings = getWorld().getSettings();
    PropertiesFile* const pf = settings.getUserSettings();

    mainWindow.reset (new MainWindow (world));
    mainWindow->setContentNonOwned (getContentComponent(), true);
    mainWindow->centreWithSize (content->getWidth(), content->getHeight());
    mainWindow->restoreWindowStateFromString (pf->getValue ("mainWindowState"));
    mainWindow->addKeyListener (keys.get());
    mainWindow->addKeyListener (commander().getKeyMappings());
    getContentComponent()->restoreState (pf);

    {
        const auto stateName = settings.getWorkspace();
        WorkspaceState state = WorkspaceState::loadByFileOrName (stateName);
        if (! state.isValid())
            state = WorkspaceState::loadByName ("Classic");
        getContentComponent()->applyWorkspaceState (state);
    }

    mainWindow->addToDesktop();


    Desktop::getInstance().setGlobalScaleFactor(
        getWorld().getSettings().getDesktopScale());
    
    if (pf->getBoolValue ("mainWindowVisible", true))
    {
        mainWindow->setVisible (true);
        if (pf->getBoolValue ("mainWindowFullScreen", false))
            mainWindow->setFullScreen (true);
    }
    else
    {
        mainWindow->setVisible (false);
        mainWindow->removeFromDesktop();
    }
    
    findSibling<SessionController>()->resetChanges();
    refreshSystemTray();
    stabilizeViews();
}

SessionRef GuiController::session()
{
    if (! sessionRef)
        sessionRef = world.getSession();
    return sessionRef;
}

ApplicationCommandTarget* GuiController::getNextCommandTarget()
{
    return findSibling<WorkspacesController>();
}

void GuiController::getAllCommands (Array <CommandID>& commands)
{
    commands.addArray ({
       #if defined (EL_PRO)
        Commands::showSessionConfig,
        Commands::showGraphMixer,
       #endif
       #if defined (EL_SOLO) || defined (EL_PRO)
        Commands::toggleChannelStrip,
       #endif
        Commands::showAbout,
		Commands::showPluginManager,
		Commands::showPreferences,
        Commands::showGraphConfig,
        Commands::showPatchBay,
        Commands::showGraphEditor,
        Commands::showLastContentView,
        Commands::toggleVirtualKeyboard,
        Commands::rotateContentView,
        Commands::showAllPluginWindows,
        Commands::hideAllPluginWindows,
        Commands::showKeymapEditor,
        Commands::showControllerDevices,
        Commands::toggleUserInterface,
        Commands::showConsole
    });
    
    commands.add (Commands::quit);
}

void GuiController::getCommandInfo (CommandID commandID, ApplicationCommandInfo& result)
{
    typedef ApplicationCommandInfo Info;
    auto& app = getAppController();

    switch (commandID)
    {
        case Commands::exportAudio:
            result.setInfo ("Export Audio", "Export to an audio file", Commands::Categories::Session, 0);
            break;
        case Commands::exportMidi:
            result.setInfo ("Exort MIDI", "Export to a MIDI file", Commands::Categories::Session, 0);
            break;
        case Commands::importGraph:
            result.setInfo ("Import graph", "Import a graph into current session", Commands::Categories::Session, 0);
            break;
        case Commands::exportGraph:
            result.setInfo ("Export current graph", "Export the current graph to file", Commands::Categories::Session, 0);
            break;
        case Commands::panic:
            result.addDefaultKeypress ('p', ModifierKeys::altModifier | ModifierKeys::commandModifier);
            result.setInfo ("Panic!", "Sends all notes off to the engine", "Engine", 0);
            break;

       #ifdef EL_PRO
        // MARK: Session Commands
        case Commands::sessionClose:
            result.setInfo ("Close Session", "Close the current session", Commands::Categories::Session, 0);
            break;
        case Commands::sessionNew:
            result.addDefaultKeypress ('n', ModifierKeys::commandModifier);
            result.setInfo ("New Session", "Create a new session", Commands::Categories::Session, 0);
            break;
        case Commands::sessionOpen:
            result.addDefaultKeypress ('o', ModifierKeys::commandModifier);
            result.setInfo ("Open Session", "Open an existing session", Commands::Categories::Session, 0);
            break;
        case Commands::sessionSave:
            result.addDefaultKeypress ('s', ModifierKeys::commandModifier);
            result.setInfo ("Save Session", "Save the current session", Commands::Categories::Session, 0);
            break;
        case Commands::sessionSaveAs:
            result.addDefaultKeypress ('s', ModifierKeys::commandModifier | ModifierKeys::shiftModifier);
            result.setInfo ("Save Session As", "Save the current session with a new name", Commands::Categories::Session, 0);
            break;
        case Commands::sessionAddGraph:
            result.addDefaultKeypress ('n', ModifierKeys::shiftModifier | ModifierKeys::commandModifier);
            result.setInfo ("Add graph", "Add a new graph to the session", Commands::Categories::Session, 0);
            break;
        case Commands::sessionDuplicateGraph:
            result.addDefaultKeypress ('d', ModifierKeys::shiftModifier | ModifierKeys::commandModifier);
            result.setInfo ("Duplicate current graph", "Duplicates the currently active graph", Commands::Categories::Session, 0);
            break;
        case Commands::sessionDeleteGraph:
            result.addDefaultKeypress (KeyPress::backspaceKey, ModifierKeys::commandModifier);
            result.setInfo ("Delete current graph", "Deletes the current graph", Commands::Categories::Session, 0);
            break;
        case Commands::sessionInsertPlugin:
            result.addDefaultKeypress ('p', ModifierKeys::commandModifier);
            result.setInfo ("Insert plugin", "Add a plugin in the current graph", Commands::Categories::Session, Info::isDisabled);
            break;
        case Commands::showSessionConfig:
        {
            int flags = (content != nullptr) ? 0 : Info::isDisabled;
            if (content && content->getMainViewName() == "SessionSettings") flags |= Info::isTicked;
            result.setInfo ("Session Settings", "Session Settings", Commands::Categories::Session, flags);
        } break;
       #endif

       #ifndef EL_PRO
        // MARK: Graph Commands
        case Commands::graphNew:
            result.addDefaultKeypress ('n', ModifierKeys::commandModifier);
            result.setInfo ("New Graph", "Create a new session", Commands::Categories::Session, 0);
            break;
        case Commands::graphOpen:
            result.addDefaultKeypress ('o', ModifierKeys::commandModifier);
            result.setInfo ("Open Graph", "Open an existing session", Commands::Categories::Session, 0);
            break;
        case Commands::graphSave:
            result.addDefaultKeypress ('s', ModifierKeys::commandModifier);
            result.setInfo ("Save Graph", "Save the current session", Commands::Categories::Session, 0);
            break;
        case Commands::graphSaveAs:
            result.addDefaultKeypress ('s', ModifierKeys::commandModifier | ModifierKeys::shiftModifier);
            result.setInfo ("Save Graph As", "Save the current session with a new name", Commands::Categories::Session, 0);
            break;
        case Commands::importSession:
            // result.addDefaultKeypress ('s', ModifierKeys::commandModifier | ModifierKeys::shiftModifier);
            result.setInfo ("Import Session", "Import a graph from a session", 
                Commands::Categories::Session, 0);
            break;
       #endif

        // MARK: Media Commands
        case Commands::mediaNew:
            result.setInfo ("New Media", "Close the current media", Commands::Categories::Session, 0);
            break;
        case Commands::mediaClose:
            result.setInfo ("Close Media", "Close the current media", Commands::Categories::Session, 0);
            break;
        case Commands::mediaOpen:
            result.setInfo ("Open Media", "Opens a type of supported media", Commands::Categories::Session, 0);
            break;
        case Commands::mediaSave:
            result.setInfo ("Save Media", "Saves the currently viewed object", Commands::Categories::Session, 0);
            break;
        
        case Commands::mediaSaveAs:
            result.setInfo ("Save Media As", "Saves the current object with another name", Commands::Categories::Session, 0);
            break;
            
        // MARK: Show Commands
        case Commands::showPreferences:
            result.setInfo ("Show Preferences", "Element Preferences", Commands::Categories::Application, 0);
            result.addDefaultKeypress (',', ModifierKeys::commandModifier);
            break;
        case Commands::showAbout:
            result.setInfo ("Show About", "About this program", Commands::Categories::Application, 0);
            break;
        case Commands::showLegacyView:
            result.setInfo ("Legacy View", "Shows the legacy Beat Thang Virtual GUI", 
                            Commands::Categories::UserInterface, 0);
            break;
        case Commands::showPluginManager:
            result.setInfo ("Plugin Manager", "Element Plugin Management", Commands::Categories::Application, 0);
            break;
        
        case Commands::showLastContentView:
            result.setInfo ("Last View", "Shows the last content view", Commands::Categories::UserInterface, 0);
            break;

        case Commands::showGraphConfig:
        {
            int flags = (content != nullptr) ? 0 : Info::isDisabled;
            if (content && content->getMainViewName() == "GraphSettings") flags |= Info::isTicked;
            result.setInfo ("Graph Settings", "Graph Settings", Commands::Categories::Session, flags);
        } break;
        
        case Commands::showPatchBay: {
            int flags = (content != nullptr) ? 0 : Info::isDisabled;
            if (content && content->getMainViewName() == "PatchBay") flags |= Info::isTicked;
            result.addDefaultKeypress (KeyPress::F1Key, 0);
            result.setInfo ("Patch Bay", "Show the patch bay", Commands::Categories::Session, flags);
        } break;
        
        case Commands::showGraphEditor: {
            int flags = (content != nullptr) ? 0 : Info::isDisabled;
            if (content && content->getMainViewName() == "GraphEditor") flags |= Info::isTicked;
            result.addDefaultKeypress (KeyPress::F2Key, 0);
            result.setInfo ("Graph Editor", "Show the graph editor", 
                Commands::Categories::UserInterface, flags);
        } break;



       #if defined (EL_PRO)
        case Commands::showGraphMixer: {
            int flags = (content != nullptr) ? 0 : Info::isDisabled;
            if (content && content->showAccessoryView() && 
                content->getAccessoryViewName() == EL_VIEW_GRAPH_MIXER)
            {
                flags |= Info::isTicked;
            }
            result.setInfo ("Graph Mixer", "Show/hide the graph mixer", 
                Commands::Categories::UserInterface, flags);
        } break;
        
        case Commands::showConsole: {
            int flags = (content != nullptr) ? 0 : Info::isDisabled;
            if (content && content->showAccessoryView() && 
                content->getAccessoryViewName() == EL_VIEW_CONSOLE)
            {
                flags |= Info::isTicked;
            }
            result.setInfo ("Console", "Show the scripting console", 
                Commands::Categories::UserInterface, flags);
        } break;
       #endif

        case Commands::showControllerDevices:
        {
            int flags = (content != nullptr) ? 0 : Info::isDisabled;
            if (content && content->getMainViewName() == "ControllerDevicesView") flags |= Info::isTicked;
            result.setInfo ("Controller Devices", "Show the session's controllers", 
                Commands::Categories::Session, flags);
        } break;
        
        case Commands::toggleUserInterface:
            result.setInfo ("Show/Hide UI", "Toggles visibility of the user interface", 
                Commands::Categories::UserInterface, 0);
            break;

       #if defined (EL_PRO) || defined (EL_SOLO)
        case Commands::toggleChannelStrip:
        {
            int flags = (content != nullptr) ? 0 : Info::isDisabled;
            if (content && content->isNodeChannelStripVisible()) flags |= Info::isTicked;
            result.setInfo ("Channel Strip", "Toggles the global channel strip", 
                Commands::Categories::UserInterface, flags);
        } break;
       #endif
        
        case Commands::toggleVirtualKeyboard:
        {
            int flags = (content != nullptr) ? 0 : Info::isDisabled;
            if (content && content->isVirtualKeyboardVisible()) flags |= Info::isTicked;
            result.setInfo ("Virtual Keyboard", "Toggle the virtual keyboard", 
                Commands::Categories::UserInterface, flags);
        } break;
        
        case Commands::rotateContentView:
            result.addDefaultKeypress ('r', ModifierKeys::commandModifier | ModifierKeys::altModifier);
            result.setInfo ("Rotate View", "Show the graph editor", Commands::Categories::Session, 0);
            break;

        case Commands::showAllPluginWindows:
            result.addDefaultKeypress ('w', ModifierKeys::commandModifier | ModifierKeys::altModifier | ModifierKeys::shiftModifier);
            result.setInfo ("Show all plugin windows", "Show all plugins for the current graph.", Commands::Categories::Session, 0);
            break;
        case Commands::hideAllPluginWindows:
            result.addDefaultKeypress ('w', ModifierKeys::commandModifier | ModifierKeys::altModifier);
            result.setInfo ("Hide all plugin windows", "Hides all plugins on the current graph.", Commands::Categories::Session, 0);
            break;
        case Commands::showKeymapEditor:
            // result.addDefaultKeypress ('w', ModifierKeys::commandModifier | ModifierKeys::altModifier | ModifierKeys::shiftModifier);
            result.setInfo ("Keymap Editor", "Show the keyboard shortcuts and edit them.", 
                            Commands::Categories::UserInterface, 0);
            break;

        case Commands::checkNewerVersion:
            result.setInfo ("Check For Updates", "Check newer version", 
                            Commands::Categories::Application, 0);
            break;
            
        case Commands::signIn:
            result.setInfo ("Sign In", "Saves the current object with another name", 
                            Commands::Categories::Application, 0);
            break;
        case Commands::signOut:
            result.setInfo ("Sign Out", "Saves the current object with another name", 
                            Commands::Categories::Application,   0);
            break;
            
        case Commands::quit:
            result.setInfo ("Quit", "Quit the app", Commands::Categories::Application, 0);
            result.addDefaultKeypress ('q', ModifierKeys::commandModifier);
            break;
        
        case Commands::undo: {
            int flags = getAppController().getUndoManager().canUndo() ? 0 : Info::isDisabled;
            result.setInfo ("Undo", "Undo the last operation", 
                Commands::Categories::Application, flags);
            result.addDefaultKeypress ('z', ModifierKeys::commandModifier);
        } break;
        case Commands::redo: {
            bool canRedo = getAppController().getUndoManager().canRedo();
            int flags = canRedo ? 0 : Info::isDisabled;
            result.setInfo ("Redo", "Redo the last operation", Commands::Categories::Application, flags);
            result.addDefaultKeypress ('z', ModifierKeys::commandModifier | ModifierKeys::shiftModifier);
        } break;
        
        case Commands::cut:
            result.setInfo ("Cut", "Cut", Commands::Categories::Application, 0);
            break;
        case Commands::copy:
            result.addDefaultKeypress ('c', ModifierKeys::commandModifier);
            result.setInfo ("Copy", "Copy", Commands::Categories::Application, Info::isDisabled);
            break;
        case Commands::paste:
            result.addDefaultKeypress ('p', ModifierKeys::commandModifier);
            result.setInfo ("Paste", "Paste", Commands::Categories::Application, Info::isDisabled);
            break;
        case Commands::selectAll:
            result.setInfo ("Select All", "Select all", Commands::Categories::Application, 0);
            break;
            
        case Commands::transportRewind:
            result.setInfo ("Rewind", "Transport Rewind", "Engine", 0);
            result.addDefaultKeypress ('j', 0);
            break;
        case Commands::transportForward:
            result.setInfo ("Forward", "Transport Fast Forward", "Engine", 0);
            result.addDefaultKeypress ('l', 0);
            break;
        case Commands::transportPlay:
            result.setInfo ("Play", "Transport Play", "Engine", 0);
            result.addDefaultKeypress (KeyPress::spaceKey, 0);
            break;
        case Commands::transportRecord:
            result.setInfo ("Record", "Transport Record", "Engine", 0);
            break;
        case Commands::transportSeekZero:
            result.setInfo ("Seek Start", "Seek to Beginning", "Engine", 0);
            break;
        case Commands::transportStop:
            result.setInfo ("Stop", "Transport Stop", "Engine", 0);
            break;

        case Commands::recentsClear:
            result.setInfo ("Clear Recent Files", "Clears the recently opened files list",
                            Commands::Categories::Application, 0);
            result.setActive (app.getRecentlyOpenedFilesList().getNumFiles() > 0);
            break;
    }
}

bool GuiController::perform (const InvocationInfo& info)
{
    bool result = true;
    switch (info.commandID)
    {
        case Commands::showAbout:
            toggleAboutScreen();
            break;
        case Commands::showControllerDevices: {
            content->setMainView ("ControllerDevicesView");
        } break;
        case Commands::showKeymapEditor:
            content->setMainView ("KeymapEditorView");
            break;
        case Commands::showPluginManager:
            content->setMainView ("PluginManager");
            break;
        case Commands::showPreferences:
            runDialog (ELEMENT_PREFERENCES);
            break;
        case Commands::showSessionConfig:
            content->setMainView ("SessionSettings");
            break;
        case Commands::showGraphConfig:
            content->setMainView ("GraphSettings");
            break;
        case Commands::showPatchBay:
            content->setMainView ("PatchBay");
            break;
        case Commands::showGraphEditor:
            content->setMainView ("GraphEditor");
            break;
        case Commands::showGraphMixer:
        {
            if (content->showAccessoryView() && content->getAccessoryViewName() == EL_VIEW_GRAPH_MIXER)
            {
                content->setShowAccessoryView (false);
            }
            else
            {
                content->setAccessoryView (EL_VIEW_GRAPH_MIXER);
            }
        } break;
        case Commands::showConsole:
        {            
            if (content->showAccessoryView() && content->getAccessoryViewName() == EL_VIEW_CONSOLE)
            {
                content->setShowAccessoryView (false);
            }
            else
            {
                content->setAccessoryView (EL_VIEW_CONSOLE);
            }
        } break;
        
        case Commands::toggleVirtualKeyboard:
            content->toggleVirtualKeyboard();
            break;

        case Commands::toggleChannelStrip: {
           #if defined (EL_PRO) || defined (EL_SOLO)
            content->setNodeChannelStripVisible (! content->isNodeChannelStripVisible());
           #else
            content->setNodeChannelStripVisible (false);
           #endif
        } break;

        case Commands::showLastContentView:
            content->backMainView();
            break;
        case Commands::rotateContentView:
            content->nextMainView();
            break;
        
        case Commands::showAllPluginWindows: {
            if (auto s = getWorld().getSession())
                showPluginWindowsFor (s->getActiveGraph(), true, true);
        } break;

        case Commands::hideAllPluginWindows: {
                closeAllPluginWindows (false);
        } break;

        case Commands::toggleUserInterface: {
            auto session = getWorld().getSession();
            if (auto* const window = mainWindow.get())
            {
                if (window->isOnDesktop())
                {
                    window->removeFromDesktop();
                    closeAllPluginWindows (true);
                }
                else
                {
                    window->addToDesktop();
                    window->toFront (true);
                    if (session)
                        showPluginWindowsFor (session->getActiveGraph(), true, false);
                }   
            }
        } break;

        case Commands::quit:
            JUCEApplication::getInstance()->systemRequestedQuit();
            break;
        
        default:
            result = false;
            break;
    }
    
    if (result && mainWindow)
    {
        mainWindow->refreshMenu();
    }
    
    return result;
}

void GuiController::stabilizeContent()
{
    if (auto* cc = content.get())
        cc->stabilize();
    refreshMainMenu();
    if (mainWindow)
        mainWindow->refreshName();
}

void GuiController::stabilizeViews()
{
    if (auto* cc = content.get())
    {
        const auto shouldBeEnabled = true;
        if (cc->isEnabled() != shouldBeEnabled)
            cc->setEnabled (shouldBeEnabled);
        cc->stabilizeViews();
    }
}

void GuiController::refreshSystemTray()
{
    // stabilize systray
    auto& settings = getWorld().getSettings();
    SystemTray::setEnabled (settings.isSystrayEnabled());
}

void GuiController::refreshMainMenu()
{
    if (auto* win = mainWindow.get())
        win->refreshMenu();
}

void GuiController::toggleAboutScreen()
{
    if (! about)
    {
        about.reset (new AboutDialog (*this));
    }

    jassert (about);

    if (about->isOnDesktop())
    {
        about->removeFromDesktop();
        about->setVisible (false);
    }
    else
    {
        about->addToDesktop();
        about->centreWithSize (about->getWidth(), about->getHeight());
        about->setVisible (true);
        about->toFront (true);
        if (getRunMode() == RunMode::Plugin)
            about->setAlwaysOnTop (true);
    }
}

KeyListener* GuiController::getKeyListener() const { return keys.get(); }

bool GuiController::handleMessage (const AppMessage& msg)
{
    if (auto m = dynamic_cast<const ReloadMainContentMessage*> (&msg))
    {
        auto& settings = getWorld().getSettings();
        PropertiesFile* const pf = settings.getUserSettings();

        if (mainWindow && pf != nullptr)
        {
            const auto ws = mainWindow->getWindowStateAsString();
            mainWindow->clearContentComponent();
            content.reset (nullptr);
            mainWindow->setContentNonOwned (getContentComponent(), true);
            mainWindow->restoreWindowStateFromString (ws);
            
            const auto stateName = settings.getWorkspace();
            WorkspaceState state = WorkspaceState::loadByFileOrName (stateName);
            if (! state.isValid())
                state = WorkspaceState::loadByName ("Classic");
            getContentComponent()->applyWorkspaceState (state);
            stabilizeContent();
        }
    
        return true;
    }
    else if (auto m = dynamic_cast<const PresentViewMessage*> (&msg))
    {
        if (m->create)
            if (auto* v = m->create())
                getContentComponent()->setMainView (v);
        
        return true;
    }

    return false;
}

void GuiController::changeListenerCallback (ChangeBroadcaster* broadcaster)
{
    if (broadcaster == &getWorld().getDeviceManager())
        if (auto* win = mainWindow.get())
            win->refreshMenu();
}

void GuiController::clearContentComponent()
{
    if (about)
    {
        about->setVisible (false);
        about->removeFromDesktop();
        about = nullptr;
    }

    if (mainWindow)
    {
        jassertfalse; // This should only ever be called from 
                      // an Element plugin instance
        mainWindow->clearContentComponent();
    }

    jassert (content != nullptr);
    content = nullptr;
}

}
