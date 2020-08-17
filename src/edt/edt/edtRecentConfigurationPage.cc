
/*

  KLayout Layout Viewer
  Copyright (C) 2006-2020 Matthias Koefferlein

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

#include "edtRecentConfigurationPage.h"
#include "edtUtils.h"
#include "layDispatcher.h"
#include "dbLibraryManager.h"
#include "dbLibrary.h"

#include <QVBoxLayout>
#include <QHeaderView>
#include <QLabel>

namespace edt
{

static const size_t max_entries = 100;

void
RecentConfigurationPage::init ()
{
  QVBoxLayout *ly = new QVBoxLayout (this);
  ly->setMargin (0);

  QLabel *label = new QLabel (this);
  label->setText (tr ("Click to select a recent configuration"));
  ly->addWidget (label);

  mp_tree_widget = new QTreeWidget (this);
  mp_tree_widget->setRootIsDecorated (false);
  mp_tree_widget->setUniformRowHeights (true);
  mp_tree_widget->setSelectionMode (QAbstractItemView::NoSelection);
  mp_tree_widget->setAllColumnsShowFocus (true);
  ly->addWidget (mp_tree_widget);

  connect (mp_tree_widget, SIGNAL (itemClicked (QTreeWidgetItem *, int)), this, SLOT (item_clicked (QTreeWidgetItem *)));

  mp_tree_widget->setColumnCount (int (m_cfg.size ()));

  QStringList column_labels;
  for (std::list<ConfigurationDescriptor>::const_iterator c = m_cfg.begin (); c != m_cfg.end (); ++c) {
    column_labels << tl::to_qstring (c->title);
  }
  mp_tree_widget->setHeaderLabels (column_labels);

  update_list (get_stored_values ());
}

RecentConfigurationPage::~RecentConfigurationPage ()
{
  //  .. nothing yet ..
}

std::string RecentConfigurationPage::title () const
{
  return tl::to_string (tr ("Recent"));
}

int RecentConfigurationPage::order () const
{
  return 100;
}

std::list<std::vector<std::string> >
RecentConfigurationPage::get_stored_values () const
{
  std::string serialized_list = dispatcher ()->config_get (m_recent_cfg_name);

  std::list<std::vector<std::string> > values;
  tl::Extractor ex (serialized_list.c_str ());
  while (! ex.at_end ()) {

    values.push_back (std::vector<std::string> ());
    while (! ex.at_end () && ! ex.test (";")) {
      values.back ().push_back (std::string ());
      ex.read_word_or_quoted (values.back ().back ());
      ex.test (",");
    }

  }

  return values;
}

void
RecentConfigurationPage::set_stored_values (const std::list<std::vector<std::string> > &values) const
{
  std::string serialized_list;
  for (std::list<std::vector<std::string> >::const_iterator v = values.begin (); v != values.end (); ++v) {
    if (v != values.begin ()) {
      serialized_list += ";";
    }
    for (std::vector<std::string>::const_iterator s = v->begin (); s != v->end (); ++s) {
      serialized_list += tl::to_word_or_quoted_string (*s);
      serialized_list += ",";
    }
  }

  dispatcher ()->config_set (m_recent_cfg_name, serialized_list);
}

void
RecentConfigurationPage::render_to (QTreeWidgetItem *item, int column, const std::vector<std::string> &values, RecentConfigurationPage::ConfigurationRendering rendering)
{
  //  store original value
  item->setData (column, Qt::UserRole, tl::to_qstring (values [column]));

  switch (rendering) {

  case RecentConfigurationPage::ArrayFlag:
  case RecentConfigurationPage::Bool:
    {
      bool f = false;
      tl::from_string (values [column], f);
      static QString checkmark = QString::fromUtf8 ("\xe2\x9c\x93");
      item->setText (column, f ? checkmark : QString ()); // "checkmark"
    }
    break;

  case RecentConfigurationPage::Layer:
    // @@@
    item->setText (column, tl::to_qstring (values [column]));
    break;

  case RecentConfigurationPage::Int:
  case RecentConfigurationPage::Double:
  case RecentConfigurationPage::Text:
  case RecentConfigurationPage::CellLibraryName:
    item->setText (column, tl::to_qstring (values [column]));
    break;

  case RecentConfigurationPage::IntIfArray:
  case RecentConfigurationPage::DoubleIfArray:
    {
      bool is_array = false;
      int flag_column = 0;
      for (std::list<ConfigurationDescriptor>::const_iterator c = m_cfg.begin (); c != m_cfg.end (); ++c, ++flag_column) {
        if (c->rendering == RecentConfigurationPage::ArrayFlag) {
          tl::from_string (values [flag_column], is_array);
          break;
        }
      }

      if (is_array) {
        item->setText (column, tl::to_qstring (values [column]));
      } else {
        item->setText (column, QString ());
      }
    }
    break;

  case RecentConfigurationPage::CellDisplayName:
    {
      //  search for a libname
      int libname_column = 0;
      const db::Library *lib = 0;
      for (std::list<ConfigurationDescriptor>::const_iterator c = m_cfg.begin (); c != m_cfg.end (); ++c, ++libname_column) {
        if (c->rendering == RecentConfigurationPage::CellLibraryName) {
          lib = db::LibraryManager::instance ().lib_ptr_by_name (values [libname_column]);
          break;
        }
      }

      if (lib) {

        //  search for a PCell parameters
        int pcp_column = 0;
        std::map<std::string, tl::Variant> pcp;
        for (std::list<ConfigurationDescriptor>::const_iterator c = m_cfg.begin (); c != m_cfg.end (); ++c, ++pcp_column) {
          if (c->rendering == RecentConfigurationPage::PCellParameters) {
            pcp = pcell_parameters_from_string (values [pcp_column]);
            break;
          }
        }

        std::pair<bool, db::Layout::pcell_id_type> pcid = lib->layout ().pcell_by_name (values [column].c_str ());
        if (pcid.first) {
          const db::PCellDeclaration *pc_decl = lib->layout ().pcell_declaration (pcid.second);
          if (pc_decl) {
            item->setText (column, tl::to_qstring (pc_decl->get_display_name (pc_decl->map_parameters (pcp))));
            break;
          }
        }

      }

      item->setText (column, tl::to_qstring (values [column]));
    }
    break;

  case RecentConfigurationPage::PCellParameters:
    {
      std::map<std::string, tl::Variant> pcp;
      pcp = pcell_parameters_from_string (values [column]);
      std::string r;
      for (std::map<std::string, tl::Variant>::const_iterator p = pcp.begin (); p != pcp.end (); ++p) {
        if (p != pcp.begin ()) {
          r += ",";
        }
        r += p->first;
        r += "=";
        r += p->second.to_string ();
      }

      item->setText (column, tl::to_qstring (r));
    }
    break;
  }

}

void
RecentConfigurationPage::update_list (const std::list<std::vector<std::string> > &stored_values)
{
  int row = 0;
  for (std::list<std::vector<std::string> >::const_iterator v = stored_values.begin (); v != stored_values.end (); ++v, ++row) {

    QTreeWidgetItem *item = 0;
    if (row < mp_tree_widget->topLevelItemCount ()) {
      item = mp_tree_widget->topLevelItem (row);
    } else {
      item = new QTreeWidgetItem (mp_tree_widget);
      mp_tree_widget->addTopLevelItem (item);
    }

    int column = 0;
    for (std::list<ConfigurationDescriptor>::const_iterator c = m_cfg.begin (); c != m_cfg.end (); ++c, ++column) {
      if (column < int (v->size ())) {
        render_to (item, column, *v, c->rendering);
      }
    }

  }

  while (mp_tree_widget->topLevelItemCount () > row) {
    delete mp_tree_widget->takeTopLevelItem (row);
  }

  mp_tree_widget->header ()->resizeSections (QHeaderView::ResizeToContents);
}

void
RecentConfigurationPage::item_clicked (QTreeWidgetItem *item)
{
  int column = 0;
  for (std::list<ConfigurationDescriptor>::const_iterator c = m_cfg.begin (); c != m_cfg.end (); ++c, ++column) {
    std::string v = tl::to_string (item->data (column, Qt::UserRole).toString ());
    dispatcher ()->config_set (c->cfg_name, v);
  }
  dispatcher ()->config_end ();
}

void
RecentConfigurationPage::commit_recent (lay::Dispatcher *root)
{
  std::vector<std::string> values;
  values.reserve (m_cfg.size ());
  for (std::list<ConfigurationDescriptor>::const_iterator c = m_cfg.begin (); c != m_cfg.end (); ++c) {
    values.push_back (root->config_get (c->cfg_name));
  }

  std::list<std::vector<std::string> > stored_values = get_stored_values ();

  for (std::list<std::vector<std::string> >::iterator v = stored_values.begin (); v != stored_values.end (); ++v) {
    if (*v == values) {
      stored_values.erase (v);
      break;
    }
  }

  stored_values.push_front (values);

  while (stored_values.size () > max_entries) {
    stored_values.erase (--stored_values.end ());
  }

  set_stored_values (stored_values);

  update_list (stored_values);
}

}