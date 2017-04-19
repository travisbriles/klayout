
/*

  KLayout Layout Viewer
  Copyright (C) 2006-2017 Matthias Koefferlein

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
  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA

*/

#include "layMacroController.h"
#include "layTechnologyController.h"
#include "laySaltController.h"
#include "layMacroEditorDialog.h"
#include "layMacroInterpreter.h"
#include "layMainWindow.h"
#include "layMainConfigPages.h"
#include "layConfig.h"
#include "layApplication.h"

#include <QDir>
#include <QUrl>
#include <QMessageBox>

namespace lay
{

MacroController::MacroController ()
  : mp_macro_editor (0), mp_mw (0), m_no_implicit_macros (false), m_file_watcher (0),
    dm_do_update_menu_with_macros (this, &MacroController::do_update_menu_with_macros),
    dm_do_sync_with_external_sources (this, &MacroController::do_sync_with_external_sources),
    dm_sync_file_watcher (this, &MacroController::sync_file_watcher),
    dm_sync_files (this, &MacroController::sync_files)
{
  connect (&m_temp_macros, SIGNAL (menu_needs_update ()), this, SLOT (macro_collection_changed ()));
  connect (&m_temp_macros, SIGNAL (macro_collection_changed (MacroCollection *)), this, SLOT (macro_collection_changed ()));
}

void
MacroController::load ()
{
  //  Scan built-in macros
  //  These macros are always taken, even if there are no macros requested (they are required to
  //  fully form the API).
  lay::MacroCollection::root ().add_folder (tl::to_string (QObject::tr ("Built-In")), ":/built-in-macros", "macros", true);
  lay::MacroCollection::root ().add_folder (tl::to_string (QObject::tr ("Built-In")), ":/built-in-pymacros", "pymacros", true);

  //  TODO: consider adding "drc" dynamically and allow more dynamic categories
  m_macro_categories.push_back (std::pair<std::string, std::string> ("macros", tl::to_string (QObject::tr ("Ruby"))));
  m_macro_categories.push_back (std::pair<std::string, std::string> ("pymacros", tl::to_string (QObject::tr ("Python"))));
  m_macro_categories.push_back (std::pair<std::string, std::string> ("drc", tl::to_string (QObject::tr ("DRC"))));

  //  Scan for macros and set interpreter path
  for (std::vector <InternalPathDescriptor>::const_iterator p = m_internal_paths.begin (); p != m_internal_paths.end (); ++p) {

    for (size_t c = 0; c < m_macro_categories.size (); ++c) {
      if (p->cat.empty () || p->cat == m_macro_categories [c].first) {
        std::string mp = tl::to_string (QDir (tl::to_qstring (p->path)).absoluteFilePath (tl::to_qstring (m_macro_categories [c].first)));
        lay::MacroCollection::root ().add_folder (p->description, mp, m_macro_categories [c].first, p->readonly);
      }
    }

  }
}

void
MacroController::initialized (lay::PluginRoot *root)
{
  mp_mw = dynamic_cast <lay::MainWindow *> (root);
  if (mp_mw) {
    mp_macro_editor = new lay::MacroEditorDialog (mp_mw, &lay::MacroCollection::root ());
    mp_macro_editor->setModal (false);
  }

  if (! m_file_watcher) {
    m_file_watcher = new tl::FileSystemWatcher (this);
    connect (m_file_watcher, SIGNAL (fileChanged (const QString &)), this, SLOT (file_watcher_triggered ()));
    connect (m_file_watcher, SIGNAL (fileRemoved (const QString &)), this, SLOT (file_watcher_triggered ()));
  }

  connect (&lay::MacroCollection::root (), SIGNAL (menu_needs_update ()), this, SLOT (macro_collection_changed ()));
  connect (&lay::MacroCollection::root (), SIGNAL (macro_collection_changed (MacroCollection *)), this, SLOT (macro_collection_changed ()));
  if (lay::TechnologyController::instance ()) {
    connect (lay::TechnologyController::instance (), SIGNAL (active_technology_changed ()), this, SLOT (macro_collection_changed ()));
    connect (lay::TechnologyController::instance (), SIGNAL (technologies_edited ()), this, SLOT (sync_with_external_sources ()));
  }
  if (lay::SaltController::instance ()) {
    connect (lay::SaltController::instance (), SIGNAL (salt_changed ()), this, SLOT (sync_with_external_sources ()));
  }

  //  synchronize the macro collection with all external sources
  sync_implicit_macros (false);

  //  update the menus with the macro menu bindings as late as possible (now we
  //  can be sure that the menus are created propertly)
  macro_collection_changed ();
}

void
MacroController::uninitialize (lay::PluginRoot * /*root*/)
{
  disconnect (&lay::MacroCollection::root (), SIGNAL (menu_needs_update ()), this, SLOT (macro_collection_changed ()));
  disconnect (&lay::MacroCollection::root (), SIGNAL (macro_collection_changed (MacroCollection *)), this, SLOT (macro_collection_changed ()));
  if (lay::TechnologyController::instance ()) {
    disconnect (lay::TechnologyController::instance (), SIGNAL (active_technology_changed ()), this, SLOT (macro_collection_changed ()));
    disconnect (lay::TechnologyController::instance (), SIGNAL (technologies_edited ()), this, SLOT (sync_with_external_sources ()));
  }
  if (lay::SaltController::instance ()) {
    disconnect (lay::SaltController::instance (), SIGNAL (salt_changed ()), this, SLOT (sync_with_external_sources ()));
  }

  if (m_file_watcher) {
    disconnect (m_file_watcher, SIGNAL (fileChanged (const QString &)), this, SLOT (file_watcher_triggered ()));
    disconnect (m_file_watcher, SIGNAL (fileRemoved (const QString &)), this, SLOT (file_watcher_triggered ()));
    delete m_file_watcher;
    m_file_watcher = 0;
  }

  delete mp_macro_editor;
  mp_macro_editor = 0;
  mp_mw = 0;
}

bool
MacroController::configure (const std::string &key, const std::string &value)
{
  if (key == cfg_key_bindings && mp_mw) {

    //  Update the shortcuts of the menu item if they have been edited in the configuration editor
    std::vector<std::pair<std::string, std::string> > key_bindings = unpack_key_binding (value);
    for (std::vector<std::pair<std::string, std::string> >::const_iterator kb = key_bindings.begin (); kb != key_bindings.end (); ++kb) {
      if (mp_mw->menu ()->is_valid (kb->first)) {
        lay::Action a = mp_mw->menu ()->action (kb->first);
        if (m_action_to_macro.find (a.qaction ()) != m_action_to_macro.end ()) {
          m_action_to_macro [a.qaction ()]->set_shortcut (kb->second);
        }
      }
    }

  }

  return false;
}

void
MacroController::config_finalize()
{
  //  .. nothing yet ..
}

bool
MacroController::can_exit (lay::PluginRoot * /*root*/) const
{
  if (mp_macro_editor) {
    return mp_macro_editor->can_exit ();
  } else {
    return true;
  }
}

bool
MacroController::accepts_drop (const std::string &path_or_url) const
{
  QUrl url (tl::to_qstring (path_or_url));
  QFileInfo file_info (url.path ());
  QString suffix = file_info.suffix ().toLower ();

  if (suffix == QString::fromUtf8 ("rb") ||
      suffix == QString::fromUtf8 ("py") ||
      suffix == QString::fromUtf8 ("lym")) {
    return true;
  }

  //  check the suffixes in the DSL interpreter declarations
  for (tl::Registrar<lay::MacroInterpreter>::iterator cls = tl::Registrar<lay::MacroInterpreter>::begin (); cls != tl::Registrar<lay::MacroInterpreter>::end (); ++cls) {
    if (suffix == tl::to_qstring (cls->suffix ())) {
      return true;
    }
  }

  return false;
}

void
MacroController::drop_url (const std::string &path_or_url)
{
  //  Normalize the URL to become either a normal path or a URL
  std::string path = path_or_url;

  QUrl url (tl::to_qstring (path_or_url));
  QString file_name = QFileInfo (url.path ()).fileName ();

  if (url.scheme () == QString::fromUtf8 ("file")) {
    path = tl::to_string (url.toLocalFile ());
  }

  //  load and run macro
  std::auto_ptr<lay::Macro> macro (new lay::Macro ());
  macro->load_from (path);
  macro->set_file_path (path);

  if (macro->is_autorun () || macro->show_in_menu ()) {

    //  install macro permanently
    if (QMessageBox::question (mp_mw,
                               QObject::tr ("Install Macro"),
                               QObject::tr ("Install macro '%1' permanently?\n\nPress 'Yes' to install the macro in the application settings folder permanently.").arg (file_name),
                               QMessageBox::Yes | QMessageBox::No,
                               QMessageBox::No) == QMessageBox::Yes) {

      //  Use the application data folder
      QDir folder (tl::to_qstring (lay::Application::instance ()->appdata_path ()));

      std::string cat = "macros";
      if (! macro->category ().empty ()) {
        cat = macro->category ();
      }

      if (! folder.cd (tl::to_qstring (cat))) {
        throw tl::Exception (tl::to_string (QObject::tr ("Folder '%s' does not exists in installation path '%s' - cannot install")).c_str (), cat, lay::Application::instance ()->appdata_path ());
      }

      QFileInfo target (folder, file_name);

      if (! target.exists () || QMessageBox::question (mp_mw,
                                                       QObject::tr ("Overwrite Macro"),
                                                       QObject::tr ("Overwrite existing macro?"),
                                                       QMessageBox::Yes | QMessageBox::No,
                                                       QMessageBox::No) == QMessageBox::Yes) {

        QFile target_file (target.filePath ());
        if (target.exists () && ! target_file.remove ()) {
          throw tl::Exception (tl::to_string (QObject::tr ("Unable to remove file '%1'").arg (target.filePath ())));
        }

        macro->set_file_path (tl::to_string (target.filePath ()));

        //  run the macro now - if it fails, it is not installed, but the file path is already set to
        //  the target path.
        if (macro->is_autorun ()) {
          macro->run ();
        }

        macro->save ();

      }

    } else {

      if (macro->is_autorun ()) {
        //  If it is not installed, run it now ..
        macro->run ();
      } else if (macro->show_in_menu ()) {
        //  .. or add as temporary macro so it is shown in the menu.
        add_temp_macro (macro.release ());
      }

    }

  } else {
    macro->run ();
  }
}

void
MacroController::show_editor (const std::string &cat, bool force_add)
{
  if (mp_macro_editor) {
    mp_macro_editor->show (cat, force_add);
  }
}

void
MacroController::enable_implicit_macros (bool enable)
{
  m_no_implicit_macros = !enable;
}

void
MacroController::sync_implicit_macros (bool ask_before_autorun)
{
  if (m_no_implicit_macros) {
    return;
  }

  std::vector<ExternalPathDescriptor> external_paths;

  //  Add additional places where the technologies define some macros

  std::map<std::string, std::vector<std::string> > tech_names_by_path;

  for (lay::Technologies::const_iterator t = lay::Technologies::instance ()->begin (); t != lay::Technologies::instance ()->end (); ++t) {
    if (! t->base_path ().empty ()) {
      QDir base_dir (tl::to_qstring (t->base_path ()));
      if (base_dir.exists ()) {
        tech_names_by_path [tl::to_string (base_dir.absolutePath ())].push_back (t->name ());
      }
    }
  }

  for (std::map<std::string, std::vector<std::string> >::const_iterator t = tech_names_by_path.begin (); t != tech_names_by_path.end (); ++t) {

    for (size_t c = 0; c < macro_categories ().size (); ++c) {

      QDir base_dir (tl::to_qstring (t->first));
      QDir macro_dir (base_dir.filePath (tl::to_qstring (macro_categories () [c].first)));
      if (macro_dir.exists ()) {

        std::string description;
        if (t->second.size () == 1) {
          description = tl::to_string (tr ("Technology %1").arg (tl::to_qstring (t->second.front ())));
        } else {
          description = tl::to_string (tr ("Technologies %1").arg (tl::to_qstring (tl::join (t->second, ","))));
        }

        external_paths.push_back (ExternalPathDescriptor (tl::to_string (macro_dir.path ()), description, macro_categories () [c].first, lay::MacroCollection::TechFolder));

      }

    }

  }

  //  Add additional places where the salt defines macros

  lay::SaltController *sc = lay::SaltController::instance ();
  if (sc) {

    lay::Salt &salt = sc->salt ();
    for (lay::Salt::flat_iterator i = salt.begin_flat (); i != salt.end_flat (); ++i) {

      const lay::SaltGrain *g = *i;

      for (size_t c = 0; c < macro_categories ().size (); ++c) {

        QDir base_dir (tl::to_qstring (g->path ()));
        QDir macro_dir (base_dir.filePath (tl::to_qstring (macro_categories () [c].first)));
        if (macro_dir.exists ()) {

          std::string description = tl::to_string (tr ("Package %1").arg (tl::to_qstring (g->name ())));
          external_paths.push_back (ExternalPathDescriptor (tl::to_string (macro_dir.path ()), description, macro_categories () [c].first, lay::MacroCollection::SaltFolder));

        }

      }

    }

  }

  //  delete macro collections which are no longer required or update description

  std::vector<lay::MacroCollection *> folders_to_delete;

  //  determine the paths that will be in use
  std::map<std::string, const ExternalPathDescriptor *> new_folders_by_path;
  for (std::vector<ExternalPathDescriptor>::const_iterator p = external_paths.begin (); p != external_paths.end (); ++p) {
    new_folders_by_path.insert (std::make_pair (p->path, p.operator-> ()));
  }

  //  determine the paths currently in use
  std::map<std::string, const ExternalPathDescriptor *> prev_folders_by_path;
  for (std::vector<ExternalPathDescriptor>::const_iterator p = m_external_paths.begin (); p != m_external_paths.end (); ++p) {
    prev_folders_by_path.insert (std::make_pair (p->path, p.operator-> ()));
  }

  lay::MacroCollection *root = &lay::MacroCollection::root ();

  for (lay::MacroCollection::child_iterator m = root->begin_children (); m != root->end_children (); ++m) {
    if (m->second->virtual_mode () == lay::MacroCollection::TechFolder ||
        m->second->virtual_mode () == lay::MacroCollection::SaltFolder) {
      std::map<std::string, const ExternalPathDescriptor *>::const_iterator u = new_folders_by_path.find (m->second->path ());
      if (u == new_folders_by_path.end ()) {
        //  no longer used
        folders_to_delete.push_back (m->second);
      } else {
        m->second->set_description (u->second->description);
      }
    }
  }

  for (std::vector<lay::MacroCollection *>::iterator m = folders_to_delete.begin (); m != folders_to_delete.end (); ++m) {
    if (tl::verbosity () >= 20) {
      tl::info << "Removing macro folder " << (*m)->path () << ", category '" << (*m)->category () << "' because no longer in use";
    }
    root->erase (*m);
  }

  //  store new paths
  m_external_paths = external_paths;

  //  add new folders

  std::vector<lay::MacroCollection *> new_folders;

  for (std::vector<ExternalPathDescriptor>::const_iterator p = m_external_paths.begin (); p != m_external_paths.end (); ++p) {

    if (prev_folders_by_path.find (p->path) != prev_folders_by_path.end ()) {
      continue;
    }

    if (tl::verbosity () >= 20) {
      tl::info << "Adding macro folder " << p->path << ", category '" << p->cat << "' for '" << p->description << "'";
    }

    //  Add the folder. Note: it may happen that a macro folder for the tech specific macros already exists in
    //  a non-tech context.
    //  In that case, the add_folder method will return 0.

    //  TODO: is it wise to make this writeable?
    lay::MacroCollection *mc = lay::MacroCollection::root ().add_folder (p->description, p->path, p->cat, false);
    if (mc) {
      mc->set_virtual_mode (p->type);
      new_folders.push_back (mc);
    }

  }

  {

    //  This prevents the message dialog below to issue deferred methods
    tl::NoDeferredMethods silent;

    bool has_autorun = false;
    for (std::vector<lay::MacroCollection *>::const_iterator m = new_folders.begin (); m != new_folders.end () && ! has_autorun; ++m) {
      has_autorun = (*m)->has_autorun ();
    }

    if (has_autorun) {
      if (! ask_before_autorun || QMessageBox::question (mp_mw, QObject::tr ("Run Macros"), QObject::tr ("Some macros associated with new items are configured to run automatically.\n\nChoose 'Yes' to run these macros now. Choose 'No' to not run them."), QMessageBox::Yes | QMessageBox::No) == QMessageBox::Yes) {
        for (std::vector<lay::MacroCollection *>::const_iterator m = new_folders.begin (); m != new_folders.end (); ++m) {
          (*m)->autorun ();
        }
      }
    }

  }
}

void
MacroController::add_path (const std::string &path, const std::string &description, const std::string &category, bool readonly)
{
  m_internal_paths.push_back (InternalPathDescriptor (path, description, category, readonly));
}

void
MacroController::add_temp_macro (lay::Macro *m)
{
  m_temp_macros.add_unspecific (m);
}

void
MacroController::add_macro_items_to_menu (lay::MacroCollection &collection, int &n, std::set<std::string> &groups, const lay::Technology *tech, std::vector<std::pair<std::string, std::string> > *key_bindings)
{
  for (lay::MacroCollection::child_iterator c = collection.begin_children (); c != collection.end_children (); ++c) {

    //  check whether the macro collection is associated with the selected technology (if there is one)
    bool consider = false;
    if (! tech || c->second->virtual_mode () != lay::MacroCollection::TechFolder) {
      consider = true;
    } else {
      const std::vector<std::pair<std::string, std::string> > &mc = macro_categories ();
      for (std::vector<std::pair<std::string, std::string> >::const_iterator cc = mc.begin (); cc != mc.end () && !consider; ++cc) {
        consider = (c->second->path () == tl::to_string (QDir (tl::to_qstring (tech->base_path ())).filePath (tl::to_qstring (cc->first))));
      }
    }

    if (consider) {
      add_macro_items_to_menu (*c->second, n, groups, 0 /*don't check 2nd level and below*/, key_bindings);
    }

  }

  for (lay::MacroCollection::iterator c = collection.begin (); c != collection.end (); ++c) {

    std::string sc = tl::trim (c->second->shortcut ());

    if (c->second->show_in_menu ()) {

      std::string mp = tl::trim (c->second->menu_path ());
      if (mp.empty ()) {
        mp = "macros_menu.end";
      }

      std::string gn = tl::trim (c->second->group_name ());
      if (! gn.empty () && groups.find (gn) == groups.end ()) {
        groups.insert (gn);
        lay::Action as;
        as.set_separator (true);
        m_macro_actions.push_back (as);
        mp_mw->menu ()->insert_item (mp, "macro_in_menu_" + tl::to_string (n++), as);
      }

      lay::Action a;
      if (c->second->description ().empty ()) {
        a.set_title (c->second->path ());
      } else {
        a.set_title (c->second->description ());
      }
      a.set_shortcut (sc);
      m_macro_actions.push_back (a);
      mp_mw->menu ()->insert_item (mp, "macro_in_menu_" + tl::to_string (n++), a);

      m_action_to_macro.insert (std::make_pair (a.qaction (), c->second));

      MacroSignalAdaptor *adaptor = new MacroSignalAdaptor (a.qaction (), c->second);
      QObject::connect (a.qaction (), SIGNAL (triggered ()), adaptor, SLOT (run ()));

      //  store the key bindings in the array
      if (!sc.empty () && key_bindings) {
        key_bindings->push_back (std::make_pair (mp, sc));
      }

    } else if (! sc.empty ()) {

      //  Create actions for shortcut-only actions too and add them to the main window
      //  to register their shortcut.

      lay::Action a;
      if (c->second->description ().empty ()) {
        a.set_title (c->second->path ());
      } else {
        a.set_title (c->second->description ());
      }
      a.set_shortcut (sc);
      m_macro_actions.push_back (a);

      mp_mw->addAction (a.qaction ());
      MacroSignalAdaptor *adaptor = new MacroSignalAdaptor (a.qaction (), c->second);
      QObject::connect (a.qaction (), SIGNAL (triggered ()), adaptor, SLOT (run ()));

    }

  }
}

void
MacroController::sync_with_external_sources ()
{
  dm_do_sync_with_external_sources ();
}

void
MacroController::do_sync_with_external_sources ()
{
  try {
    sync_implicit_macros (true);
  } catch (tl::Exception &ex) {
    tl::error << ex.msg ();
  }
}

void
MacroController::macro_collection_changed ()
{
  //  empty action to macro table now we know it's invalid
  m_action_to_macro.clear ();
  dm_do_update_menu_with_macros ();
  dm_sync_file_watcher ();
}

void
MacroController::do_update_menu_with_macros ()
{
  if (!mp_mw) {
    return;
  }

  const lay::Technology *tech = 0;
  if (lay::TechnologyController::instance ()) {
    tech = lay::TechnologyController::instance ()->active_technology ();
  }

  std::vector<std::pair<std::string, std::string> > key_bindings = unpack_key_binding (mp_mw->config_get (cfg_key_bindings));
  std::sort (key_bindings.begin (), key_bindings.end ());

  std::vector<std::pair<std::string, std::string> > new_key_bindings;
  for (std::vector<std::pair<std::string, std::string> >::const_iterator kb = key_bindings.begin (); kb != key_bindings.end (); ++kb) {
    if (mp_mw->menu ()->is_valid (kb->first)) {
      lay::Action a = mp_mw->menu ()->action (kb->first);
      if (m_action_to_macro.find (a.qaction ()) == m_action_to_macro.end ()) {
        new_key_bindings.push_back (*kb);
      }
    }
  }

  //  delete all existing items
  for (std::vector<lay::Action>::iterator a = m_macro_actions.begin (); a != m_macro_actions.end (); ++a) {
    mp_mw->menu ()->delete_items (*a);
  }
  m_macro_actions.clear ();
  m_action_to_macro.clear ();

  int n = 1;
  std::set<std::string> groups;
  add_macro_items_to_menu (m_temp_macros, n, groups, tech, 0);
  add_macro_items_to_menu (lay::MacroCollection::root (), n, groups, tech, &new_key_bindings);

  //  update the key bindings if required
  std::sort (new_key_bindings.begin (), new_key_bindings.end ());
  if (new_key_bindings != key_bindings) {
    mp_mw->config_set (cfg_key_bindings, pack_key_binding (new_key_bindings));
  }
}

void
MacroController::file_watcher_triggered ()
{
  dm_sync_files ();
}

static void
add_collections_to_file_watcher (const lay::MacroCollection &collection, tl::FileSystemWatcher *watcher)
{
  for (lay::MacroCollection::const_child_iterator c = collection.begin_children (); c != collection.end_children (); ++c) {
    if (! c->second->path ().empty () && c->second->path ()[0] != ':') {
      watcher->add_file (c->second->path ());
      add_collections_to_file_watcher (*c->second, watcher);
    }
  }
}

void
MacroController::sync_file_watcher ()
{
  m_file_watcher->clear ();
  m_file_watcher->enable (false);
  add_collections_to_file_watcher (lay::MacroCollection::root (), m_file_watcher);
  m_file_watcher->enable (true);
}

void
MacroController::sync_files ()
{
  tl::log << tl::to_string (tr ("Detected file system change in macro folders - updating"));
  lay::MacroCollection::root ().reload ();
}

MacroController *
MacroController::instance ()
{
  for (tl::Registrar<lay::PluginDeclaration>::iterator cls = tl::Registrar<lay::PluginDeclaration>::begin (); cls != tl::Registrar<lay::PluginDeclaration>::end (); ++cls) {
    MacroController *mc = dynamic_cast <MacroController *> (cls.operator-> ());
    if (mc) {
      return mc;
    }
  }
  return 0;
}

//  The singleton instance of the macro controller
static tl::RegisteredClass<lay::PluginDeclaration> macro_controller_decl (new lay::MacroController (), 120, "MacroController");

}

