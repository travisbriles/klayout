
/*

  KLayout Layout Viewer
  Copyright (C) 2006-2019 Matthias Koefferlein

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


#ifndef HDR_layLibrariesView
#define HDR_layLibrariesView

#include <vector>
#include <string>
#include <algorithm>

#include <QFrame>
#include <QTreeView>

#include "dbLayout.h"
#include "layCanvasPlane.h"
#include "layViewOp.h"
#include "layLayoutView.h"
#include "layCellTreeModel.h"
#include "layWidgets.h"
#include "tlDeferredExecution.h"

class QModelIndex;
class QComboBox;
class QMenu;
class QSplitter;
class QFrame;
class QToolButton;
class QLineEdit;
class QAction;
class QCheckBox;

namespace lay
{

/**
 *  @brief A special QTreeView customization
 *
 *  A customized QTreeView that is used to receive middle-mouse-button
 *  events and processes double clicks by bypassing the standard implementation
 *  that closes and opens branches.
 */
class LibraryTreeWidget
  : public QTreeView
{
Q_OBJECT

public:
  LibraryTreeWidget (QWidget *parent, const char *name, QWidget *key_event_receiver);

signals:
  void cell_clicked (const QModelIndex &);
  void cell_double_clicked (const QModelIndex &);
  void cell_middle_clicked (const QModelIndex &);
  void search_triggered (const QString &t);

protected:
  virtual void mouseDoubleClickEvent (QMouseEvent *event);
  virtual void mousePressEvent (QMouseEvent *event);
  virtual void mouseReleaseEvent (QMouseEvent *event);
  virtual void startDrag (Qt::DropActions supportedActions);
  virtual bool focusNextPrevChild (bool next);
  virtual void keyPressEvent (QKeyEvent *event);
  virtual bool event (QEvent *event);

  QWidget *mp_key_event_receiver;
};

/**
 *  @brief The hierarchy control panel
 *
 *  The hierarchy control panel allows changing the cell shown, to
 *  browse the hierarchy and disable/enable cells
 *
 *  The class communicates with a Layout interface for
 *  retrieval of the cell hierarchy
 */
class LibrariesView
  : public QFrame,
    public tl::Object
{
Q_OBJECT

public:
  /**
   *  @brief Constructor
   *
   *  @param parent The Qt parent widget
   *  @param name The layer control panel's widget name
   */
  LibrariesView (lay::LayoutView *view, QWidget *parent = 0, const char *name = "libraries_view");

  /**
   *  @brief Destructor
   */
  ~LibrariesView ();

  /**
   *  @brief Perform the cell control panel's initialisations on the main menu
   */
  static void init_menu (lay::AbstractMenu &menu);

  /**
   *  @brief The sizeHint implementation for Qt layout management
   */
  virtual QSize sizeHint () const;

  /**
   *  @brief Changing of the background color
   */
  void set_background_color (QColor c);

  /**
   *  @brief Changing of the text color
   */
  void set_text_color (QColor c);

  /**
   *  @brief Sets the active library by name
   */
  void select_active_lib_by_name (const std::string &name);

  /**
   *  @brief Select the active cellview
   *
   *  selects the active cellview by index. The index must be
   *  a valid index within the context of the layout view.
   */
  void select_active (int lib_index);

  /**
   *  @brief Get the active cellview
   *
   *  get the active cellview index.
   */
  int active ()
  {
    return m_active_index;
  }

  /**
   *  @brief Gets the active library or 0 if there is no active library
   */
  db::Library *active_lib ();

#if 0 // @@@
  /**
   *  @brief Returns the paths of the selected cells
   */
  void selected_cells (int cv_index, std::vector<cell_path_type> &paths) const;

  /**
   *  @brief Return the path to the current cell for the given cellview index
   *
   *  The current cell is the cell that is highlighted.
   */
  void current_cell (int cv_index, cell_path_type &path) const;

  /**
   *  @brief Set the path to the current cell
   *
   *  The current cell is the cell that is highlighted. The current cv index
   *  can be obtained with the "active" method.
   */
  void set_current_cell (int cv_index, const cell_path_type &path);
#endif

  /**
   *  @brief Update the contents if necessary
   *
   *  Update the cell trees according to the hierarchy found in
   *  the layouts. This version includes a hint which cellview has changed.
   */
  void do_update_content (int cv_index);

  /**
   *  @brief Update the contents if necessary
   *
   *  Update the cell trees according to the hierarchy found in
   *  the layouts.
   */
  void do_update_content ()
  {
    do_update_content (-1);
  }

  /**
   *  @brief Event handler
   *
   *  The object subclasses the event handler in order to intercept
   *  the GTF probe events (Qt::MaxUser).
   */
  virtual bool event (QEvent *e);

  /**
   *  @brief Return true, if the tree view has the focus
   */
  bool has_focus () const;

  /**
   *  @brief Select split mode
   *  In split mode all cell trees are shown stacked
   */
  void set_split_mode (bool sbs);

  /**
   *  @brief Returns true if side-by-side mode is set
   */
  bool split_mode () const
  {
    return m_split_mode;
  }

  /**
   *  @brief Gets the layout view this panel is attached to
   */
  lay::LayoutView *view ()
  {
    return mp_view;
  }

signals:
#if 0 // @@@
  void cell_selected (cell_path_type path, int cellview_index);
#endif
  void active_library_changed (int cellview_index);

public slots:
  void clicked (const QModelIndex &index);
  void header_clicked ();
  void double_clicked (const QModelIndex &index);
  void middle_clicked (const QModelIndex &index);
  void selection_changed (int index);
  void context_menu (const QPoint &pt);
  void search_triggered (const QString &t);
  void search_edited ();
  void search_editing_finished ();
  void search_next ();
  void search_prev ();
  void cm_cell_select ();

private:
  db::Layout *mp_layout;
  bool m_enable_cb;
  lay::LayoutView *mp_view;
  std::vector <QTreeView *> mp_cell_lists;
  std::vector <QToolButton *> mp_cell_list_headers;
  std::vector <QFrame *> mp_cell_list_frames;
  std::vector <bool> m_force_close;
  std::vector <bool> m_needs_update;
  int m_active_index;
  bool m_split_mode;
  QComboBox *mp_selector;
  lay::DecoratedLineEdit *mp_search_edit_box;
  QAction *mp_case_sensitive;
  QAction *mp_use_regular_expressions;
  CellTreeModel *mp_search_model;
  QFrame *mp_search_frame;
  QCheckBox *mp_search_close_cb;
  QSplitter *mp_splitter;
  QColor m_background_color;
  QColor m_text_color;
  tl::DeferredMethod<LibrariesView> m_do_update_content_dm;
  tl::DeferredMethod<LibrariesView> m_do_full_update_content_dm;
  std::auto_ptr<QStyle> mp_tree_style;
  std::vector<tl::weak_ptr<db::Library> > m_libraries;

  //  event listener for changes in the cellview and layout
  void update_required ();

#if 0 // @@@
  //  locate the CellTreeItem in the tree corresponding to a partial path starting from p.
  CellTreeItem *find_child_item (cell_path_type::const_iterator start, cell_path_type::const_iterator end, CellTreeItem *p);
#endif

  //  get the current item
  CellTreeItem *current_item () const;

#if 0 // @@@
  //  path from index and item from path ..
  void path_from_index (const QModelIndex &index, int cv_index, cell_path_type &path) const;
  QModelIndex index_from_path (const cell_path_type &path, int cv_index);

  //  select active cellview from sender (sender must be a cell tree)
  void set_active_celltree_from_sender ();

  //  clears all widgets of the cell lists
  void clear_all ();
#endif

  //  display string of nth cellview
  std::string display_string (int n) const;

  //  forces a complete update
  void do_full_update_content ();
};

} // namespace lay

#endif

