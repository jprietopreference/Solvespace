//-----------------------------------------------------------------------------
// Our main() function, and GTK3-specific stuff to set up our windows and
// otherwise handle our interface to the operating system. Everything
// outside gtk/... should be standard C++ and OpenGL.
//
// Copyright 2015 <whitequark@whitequark.org>
//-----------------------------------------------------------------------------
#include <errno.h>
#include <sys/stat.h>
#include <unistd.h>
#include <time.h>

#include <json-c/json_object.h>
#include <json-c/json_util.h>

#include <glibmm/main.h>
#include <giomm/file.h>
#include <gtkmm/drawingarea.h>
#include <gtkmm/scrollbar.h>
#include <gtkmm/entry.h>
#include <gtkmm/hvbox.h>
#include <gtkmm/eventbox.h>
#include <gtkmm/fixed.h>
#include <gtkmm/adjustment.h>
#include <gtkmm/separatormenuitem.h>
#include <gtkmm/menuitem.h>
#include <gtkmm/checkmenuitem.h>
#include <gtkmm/radiomenuitem.h>
#include <gtkmm/radiobuttongroup.h>
#include <gtkmm/menu.h>
#include <gtkmm/menubar.h>
#include <gtkmm/applicationwindow.h>
#include <gtkmm/scrolledwindow.h>
#include <gtkmm/filechooserdialog.h>
#include <gtkmm/messagedialog.h>
#include <gtkmm/application.h>
#include <cairomm/xlib_surface.h>
#include <pangomm/fontdescription.h>
#include <gdk/gdkx.h>
#include <fontconfig/fontconfig.h>
#undef HAVE_STDINT_H /* no thanks, we have our own config.h */

#include <GL/glx.h>

#include "solvespace.h"
#include <config.h>

char RecentFile[MAX_RECENT][MAX_PATH];

#define GL_CHECK() \
    do { \
        int err = (int)glGetError(); \
        if(err) dbp("%s:%d: glGetError() == 0x%X\n", __FILE__, __LINE__, err); \
    } while (0)

/* Settings */

/* Why not just use GSettings? Two reasons. It doesn't allow to easily see
   whether the setting had the default value, and it requires to install
   a schema globally. */
static json_object *settings = NULL;

static int CnfPrepare(char *path, int pathsz) {
    // Refer to http://standards.freedesktop.org/basedir-spec/latest/

    const char *xdg_home, *home;
    xdg_home = getenv("XDG_CONFIG_HOME");
    home = getenv("HOME");

    char dir[MAX_PATH];
    int dirlen;
    if(xdg_home)
        dirlen = snprintf(dir, sizeof(dir), "%s/solvespace", xdg_home);
    else if(home)
        dirlen = snprintf(dir, sizeof(dir), "%s/.config/solvespace", home);
    else {
        dbp("neither XDG_CONFIG_HOME nor HOME is set");
        return 1;
    }

    if(dirlen >= sizeof(dir))
        oops();

    struct stat st;
    if(stat(dir, &st)) {
        if(errno == ENOENT) {
            if(mkdir(dir, 0777)) {
                dbp("cannot mkdir %s: %s", dir, strerror(errno));
                return 1;
            }
        } else {
            dbp("cannot stat %s: %s", dir, strerror(errno));
            return 1;
        }
    } else if(!S_ISDIR(st.st_mode)) {
        dbp("%s is not a directory", dir);
        return 1;
    }

    int pathlen = snprintf(path, pathsz, "%s/settings.json", dir);
    if(pathlen >= pathsz)
        oops();

    return 0;
}

static void CnfLoad() {
    char path[MAX_PATH];
    if(CnfPrepare(path, sizeof(path)))
        return;

    if(settings)
        json_object_put(settings); // deallocate

    settings = json_object_from_file(path);
    if(!settings) {
        if(errno != ENOENT)
            dbp("cannot load settings: %s", strerror(errno));

        settings = json_object_new_object();
    }
}

static void CnfSave() {
    char path[MAX_PATH];
    if(CnfPrepare(path, sizeof(path)))
        return;

    if(json_object_to_file_ext(path, settings, JSON_C_TO_STRING_PRETTY))
        dbp("cannot save settings: %s", strerror(errno));
}

void CnfFreezeInt(uint32_t val, const char *key) {
    struct json_object *jval = json_object_new_int(val);
    json_object_object_add(settings, key, jval);
    CnfSave();
}

uint32_t CnfThawInt(uint32_t val, const char *key) {
    struct json_object *jval;
    if(json_object_object_get_ex(settings, key, &jval))
        return json_object_get_int(jval);
    else return val;
}

void CnfFreezeFloat(float val, const char *key) {
    struct json_object *jval = json_object_new_double(val);
    json_object_object_add(settings, key, jval);
    CnfSave();
}

float CnfThawFloat(float val, const char *key) {
    struct json_object *jval;
    if(json_object_object_get_ex(settings, key, &jval))
        return json_object_get_double(jval);
    else return val;
}

void CnfFreezeString(const char *val, const char *key) {
    struct json_object *jval = json_object_new_string(val);
    json_object_object_add(settings, key, jval);
    CnfSave();
}

void CnfThawString(char *val, int valsz, const char *key) {
    struct json_object *jval;
    if(json_object_object_get_ex(settings, key, &jval))
        snprintf(val, valsz, "%s", json_object_get_string(jval));
}

static void CnfFreezeWindowPos(Gtk::Window *win, const char *key) {
    int x, y, w, h;
    win->get_position(x, y);
    win->get_size(w, h);

    char buf[100];
    snprintf(buf, sizeof(buf), "%s_left", key);
    CnfFreezeInt(x, buf);
    snprintf(buf, sizeof(buf), "%s_top", key);
    CnfFreezeInt(y, buf);
    snprintf(buf, sizeof(buf), "%s_width", key);
    CnfFreezeInt(w, buf);
    snprintf(buf, sizeof(buf), "%s_height", key);
    CnfFreezeInt(h, buf);

    CnfSave();
}

static void CnfThawWindowPos(Gtk::Window *win, const char *key) {
    int x, y, w, h;
    win->get_position(x, y);
    win->get_size(w, h);

    char buf[100];
    snprintf(buf, sizeof(buf), "%s_left", key);
    x = CnfThawInt(x, buf);
    snprintf(buf, sizeof(buf), "%s_top", key);
    y = CnfThawInt(y, buf);
    snprintf(buf, sizeof(buf), "%s_width", key);
    w = CnfThawInt(w, buf);
    snprintf(buf, sizeof(buf), "%s_height", key);
    h = CnfThawInt(h, buf);

    win->move(x, y);
    win->resize(w, h);
}

/* Timer */

int64_t GetMilliseconds(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return 1000 * (uint64_t) ts.tv_sec + ts.tv_nsec / 1000000;
}

static bool TimerCallback() {
    SS.GW.TimerCallback();
    SS.TW.TimerCallback();
    return false;
}

void SetTimerFor(int milliseconds) {
    Glib::signal_timeout().connect(&TimerCallback, milliseconds);
}

static bool LaterCallback() {
    SS.DoLater();
    return false;
}

void ScheduleLater() {
    Glib::signal_idle().connect(&LaterCallback);
}

/* GL wrapper */

/* Replace this with GLArea when GTK 3.16 is old enough */
class GtkGlWidget : public Gtk::DrawingArea {
public:
    GtkGlWidget() : _xpixmap(0), _glpixmap(0) {
        int attrlist[] = {
            GLX_RGBA,
            GLX_RED_SIZE, 8,
            GLX_GREEN_SIZE, 8,
            GLX_BLUE_SIZE, 8,
            GLX_DEPTH_SIZE, 24,
            GLX_DOUBLEBUFFER,
            None
        };

        _xdisplay = gdk_x11_get_default_xdisplay();
        if(!glXQueryExtension(_xdisplay, NULL, NULL)) {
            dbp("OpenGL is not supported");
            oops();
        }

        _xvisual = XDefaultVisual(_xdisplay, gdk_x11_get_default_screen());

        _xvinfo = glXChooseVisual(_xdisplay, gdk_x11_get_default_screen(), attrlist);
        if(!_xvinfo) {
            dbp("cannot create glx visual");
            oops();
        }

        _gl = glXCreateContext(_xdisplay, _xvinfo, NULL, true);
    }

    ~GtkGlWidget() {
        destroy_buffer();

        glXDestroyContext(_xdisplay, _gl);

        XFree(_xvinfo);
    }

protected:
    /* Draw on a GLX pixmap, then read pixels out and draw them on
       the Cairo context. Slower, but you get to overlay nice widgets. */
    virtual bool on_draw(const Cairo::RefPtr<Cairo::Context> &cr) {
        allocate_buffer(get_allocation());

        if(!glXMakeCurrent(_xdisplay, _glpixmap, _gl))
            oops();

        glDrawBuffer(GL_FRONT);
        on_gl_draw();
        GL_CHECK();

        Cairo::RefPtr<Cairo::XlibSurface> surface =
                Cairo::XlibSurface::create(_xdisplay, _xpixmap, _xvisual,
                                           get_allocated_width(), get_allocated_height());
        cr->set_source(surface, 0, 0);
        cr->paint();

        if(!glXMakeCurrent(_xdisplay, None, NULL))
            oops();

        return true;
    }

    virtual void on_size_allocate (Gtk::Allocation& allocation) {
        destroy_buffer();

        Gtk::DrawingArea::on_size_allocate(allocation);
    }

    virtual void on_gl_draw() = 0;

private:
    Display *_xdisplay;
    Visual *_xvisual;
    XVisualInfo *_xvinfo;
    GLXContext _gl;
    Pixmap _xpixmap;
    GLXDrawable _glpixmap;

    void destroy_buffer() {
        if(_glpixmap) {
            glXDestroyGLXPixmap(_xdisplay, _glpixmap);
            _glpixmap = 0;
        }

        if(_xpixmap) {
            XFreePixmap(_xdisplay, _xpixmap);
            _xpixmap = 0;
        }
    }

    void allocate_buffer(Gtk::Allocation allocation) {
        if(!_xpixmap) {
            _xpixmap = XCreatePixmap(_xdisplay,
                XRootWindow(_xdisplay, gdk_x11_get_default_screen()),
                allocation.get_width(), allocation.get_height(), 24);
        }

        if(!_glpixmap) {
            _glpixmap = glXCreateGLXPixmap(_xdisplay, _xvinfo, _xpixmap);
        }
    }
};

/* Editor overlay */

class GtkEditorOverlay : public Gtk::Fixed {
public:
    GtkEditorOverlay(Gtk::Widget &underlay) : _underlay(underlay) {
        add(_underlay);

        Pango::FontDescription desc;
        desc.set_family("monospace");
        desc.set_size(7000);
        _entry.override_font(desc);
        _entry.set_width_chars(30);
        _entry.set_no_show_all(true);
        add(_entry);

        _entry.signal_activate().
            connect(sigc::mem_fun(this, &GtkEditorOverlay::on_activate));
    }

    void start_editing(int x, int y, const char *val) {
        move(_entry, x, y - 4);
        _entry.set_text(val);
        if(!_entry.is_visible()) {
            _entry.show();
            _entry.grab_focus();
            _entry.add_modal_grab();
        }
    }

    void stop_editing() {
        if(_entry.is_visible())
            _entry.remove_modal_grab();
        _entry.hide();
    }

    bool is_editing() const {
        return _entry.is_visible();
    }

    sigc::signal<void, Glib::ustring> signal_editing_done() {
        return _signal_editing_done;
    }

protected:
    virtual bool on_key_press_event(GdkEventKey *event) {
        if(event->keyval == GDK_KEY_Escape) {
            stop_editing();
            return true;
        }

        return false;
    }

    virtual void on_size_allocate(Gtk::Allocation& allocation) {
        Gtk::Fixed::on_size_allocate(allocation);

        _underlay.size_allocate(allocation);
    }

    virtual void on_activate() {
        _signal_editing_done(_entry.get_text());
    }

private:
    Gtk::Widget &_underlay;
    Gtk::Entry _entry;
    sigc::signal<void, Glib::ustring> _signal_editing_done;
};

/* Graphics window */

class GtkGraphicsWidget : public GtkGlWidget {
public:
    GtkGraphicsWidget() {
        set_events(Gdk::POINTER_MOTION_MASK |
                   Gdk::BUTTON_PRESS_MASK | Gdk::BUTTON_RELEASE_MASK | Gdk::BUTTON_MOTION_MASK |
                   Gdk::SCROLL_MASK |
                   Gdk::LEAVE_NOTIFY_MASK);
        set_double_buffered(true);
    }

    void emulate_key_press(GdkEventKey *event) {
        on_key_press_event(event);
    }

protected:
    virtual bool on_configure_event(GdkEventConfigure *event) {
        _w = event->width;
        _h = event->height;

        return GtkGlWidget::on_configure_event(event);;
    }

    virtual void on_gl_draw() {
        SS.GW.Paint();
    }

    virtual bool on_motion_notify_event(GdkEventMotion *event) {
        int x, y;
        ij_to_xy(event->x, event->y, x, y);

        SS.GW.MouseMoved(x, y,
            event->state & Gdk::BUTTON1_MASK,
            event->state & Gdk::BUTTON2_MASK,
            event->state & Gdk::BUTTON3_MASK,
            event->state & Gdk::SHIFT_MASK,
            event->state & Gdk::CONTROL_MASK);

        return true;
    }

    virtual bool on_button_press_event(GdkEventButton *event) {
        int x, y;
        ij_to_xy(event->x, event->y, x, y);

        switch(event->button) {
            case 1:
            if(event->type == Gdk::BUTTON_PRESS)
                SS.GW.MouseLeftDown(x, y);
            else if(event->type == Gdk::DOUBLE_BUTTON_PRESS)
                SS.GW.MouseLeftDoubleClick(x, y);
            break;

            case 2:
            case 3:
            SS.GW.MouseMiddleOrRightDown(x, y);
            break;
        }

        return true;
    }

    virtual bool on_button_release_event(GdkEventButton *event) {
        int x, y;
        ij_to_xy(event->x, event->y, x, y);

        switch(event->button) {
            case 1:
            SS.GW.MouseLeftUp(x, y);
            break;

            case 3:
            SS.GW.MouseRightUp(x, y);
            break;
        }

        return true;
    }

    virtual bool on_scroll_event(GdkEventScroll *event) {
        int x, y;
        ij_to_xy(event->x, event->y, x, y);

        int delta_y = event->delta_y;
        if(delta_y == 0) {
            switch(event->direction) {
                case GDK_SCROLL_UP:
                delta_y = -1;
                break;

                case GDK_SCROLL_DOWN:
                delta_y = 1;
                break;

                default:
                /* do nothing */
                return false;
            }
        }

        SS.GW.MouseScroll(x, y, delta_y);

        return true;
    }

    virtual bool on_leave_notify_event (GdkEventCrossing*event) {
        SS.GW.MouseLeave();

        return true;
    }

    virtual bool on_key_press_event(GdkEventKey *event) {
        int chr;

        switch(event->keyval) {
            case GDK_KEY_Escape:
            chr = GraphicsWindow::ESCAPE_KEY;
            break;

            case GDK_KEY_Delete:
            chr = GraphicsWindow::DELETE_KEY;
            break;

            case GDK_KEY_Tab:
            chr = '\t';
            break;

            case GDK_KEY_BackSpace:
            case GDK_KEY_Back:
            chr = '\b';
            break;

            default:
            if(event->keyval >= GDK_KEY_F1 && event->keyval <= GDK_KEY_F12)
                chr = GraphicsWindow::FUNCTION_KEY_BASE + (event->keyval - GDK_KEY_F1);
            else
                chr = gdk_keyval_to_unicode(event->keyval);
        }

        if(event->state & Gdk::SHIFT_MASK)
            chr |= GraphicsWindow::SHIFT_MASK;
        if(event->state & Gdk::CONTROL_MASK)
            chr |= GraphicsWindow::CTRL_MASK;

        if(chr && SS.GW.KeyDown(chr))
            return true;

        return false;
    }

private:
    int _w, _h;
    void ij_to_xy(int i, int j, int &x, int &y) {
        // Convert to xy (vs. ij) style coordinates,
        // with (0, 0) at center
        x = i - _w / 2;
        y = _h / 2 - j;
    }
};

class GtkGraphicsWindow : public Gtk::Window {
public:
    GtkGraphicsWindow() : _overlay(_widget) {
        CnfThawWindowPos(this, "GraphicsWindow");

        _box.pack_start(_menubar, false, true);
        _box.pack_start(_overlay, true, true);

        add(_box);

        _overlay.signal_editing_done().
            connect(sigc::mem_fun(this, &GtkGraphicsWindow::on_editing_done));
    }

    GtkGraphicsWidget &get_widget() {
        return _widget;
    }

    GtkEditorOverlay &get_overlay() {
        return _overlay;
    }

    Gtk::MenuBar &get_menubar() {
        return _menubar;
    }

    bool is_fullscreen() const {
        return _is_fullscreen;
    }

protected:
    virtual void on_hide() {
        CnfFreezeWindowPos(this, "GraphicsWindow");

        Gtk::Window::on_hide();
    }

    virtual bool on_delete_event(GdkEventAny *event) {
        SS.Exit();

        return true;
    }

    virtual bool on_window_state_event(GdkEventWindowState *event) {
        _is_fullscreen = event->new_window_state & GDK_WINDOW_STATE_FULLSCREEN;

        /* The event arrives too late for the caller of ToggleFullScreen
           to notice state change; and it's possible that the WM will
           refuse our request, so we can't just toggle the saved state */
        SS.GW.EnsureValidActives();

        return Gtk::Window::on_window_state_event(event);
    }

    virtual void on_editing_done(Glib::ustring value) {
        SS.GW.EditControlDone(value.c_str());
    }

private:
    GtkGraphicsWidget _widget;
    GtkEditorOverlay _overlay;
    Gtk::MenuBar _menubar;
    Gtk::VBox _box;

    bool _is_fullscreen;
};

GtkGraphicsWindow *GtkGW = NULL;

void GetGraphicsWindowSize(int *w, int *h) {
    *w = GtkGW->get_widget().get_allocated_width();
    *h = GtkGW->get_widget().get_allocated_height();
}

void InvalidateGraphics(void) {
    GtkGW->get_widget().queue_draw();
}

void PaintGraphics(void) {
    GtkGW->get_widget().queue_draw();
    /* Process animation */
    Glib::MainContext::get_default()->iteration(false);
}

void SetWindowTitle(const char *str) {
    GtkGW->set_title(str);
}

void ToggleFullScreen(void) {
    if(GtkGW->is_fullscreen())
        GtkGW->unfullscreen();
    else
        GtkGW->fullscreen();
}

bool FullScreenIsActive(void) {
    return GtkGW->is_fullscreen();
}

void ShowGraphicsEditControl(int x, int y, char *val) {
    Gdk::Rectangle rect = GtkGW->get_widget().get_allocation();

    // Convert to ij (vs. xy) style coordinates,
    // and compensate for the input widget height due to inverse coord
    int i, j;
    i = x + rect.get_width() / 2;
    j = -y + rect.get_height() / 2 - 24;

    GtkGW->get_overlay().start_editing(i, j, val);
}

void HideGraphicsEditControl(void) {
    GtkGW->get_overlay().stop_editing();
}

bool GraphicsEditControlIsVisible(void) {
    return GtkGW->get_overlay().is_editing();
}

/* TODO: removing menubar breaks accelerators. */
void ToggleMenuBar(void) {
    GtkGW->get_menubar().set_visible(!GtkGW->get_menubar().is_visible());
}

bool MenuBarIsVisible(void) {
    return GtkGW->get_menubar().is_visible();
}

/* Context menus */

static int context_menu_choice;

class ContextMenuItem : public Gtk::MenuItem {
public:
    ContextMenuItem(const Glib::ustring &label, int id, bool mnemonic=false) :
            Gtk::MenuItem(label, mnemonic), _id(id) {
    }

protected:
    virtual void on_activate() {
        Gtk::MenuItem::on_activate();

        if(has_submenu())
            return;

        context_menu_choice = _id;
    }

    /* Workaround for https://bugzilla.gnome.org/show_bug.cgi?id=695488.
       This is used in addition to on_activate() to catch mouse events.
       Without on_activate(), it would be impossible to select a menu item
       via keyboard.
       This selects the item twice in some cases, but we are idempotent.
     */
    virtual bool on_button_press_event(GdkEventButton *event) {
        if(event->button == 1 && event->type == Gdk::BUTTON_PRESS) {
            on_activate();
            return true;
        }

        return Gtk::MenuItem::on_button_press_event(event);
    }

private:
    int _id;
};

static Gtk::Menu *context_menu = NULL, *context_submenu = NULL;

void AddContextMenuItem(const char *label, int id) {
    Gtk::MenuItem *menu_item;
    if(label)
        menu_item = new ContextMenuItem(label, id);
    else
        menu_item = new Gtk::SeparatorMenuItem();

    if(id == CONTEXT_SUBMENU) {
        menu_item->set_submenu(*context_submenu);
        context_submenu = NULL;
    }

    if(context_submenu) {
        context_submenu->append(*menu_item);
    } else {
        if(!context_menu)
            context_menu = new Gtk::Menu;

        context_menu->append(*menu_item);
    }
}

void CreateContextSubmenu(void) {
    if(context_submenu) oops();

    context_submenu = new Gtk::Menu;
}

int ShowContextMenu(void) {
    if(!context_menu)
        return -1;

    Glib::RefPtr<Glib::MainLoop> loop = Glib::MainLoop::create();
    context_menu->signal_deactivate().
        connect(sigc::mem_fun(loop.operator->(), &Glib::MainLoop::quit));

    context_menu_choice = -1;

    context_menu->show_all();
    context_menu->popup(3, GDK_CURRENT_TIME);

    loop->run();

    delete context_menu;
    context_menu = NULL;

    return context_menu_choice;
}

/* Main menu */

template<class MenuItem> class MainMenuItem : public MenuItem {
public:
    MainMenuItem(const GraphicsWindow::MenuEntry &entry) :
            MenuItem(), _entry(entry) {
        Glib::ustring label(_entry.label);
        for(int i = 0; i < label.length(); i++) {
            if(label[i] == '&')
                label.replace(i, 1, "_");
        }

        guint accel_key = 0;
        Gdk::ModifierType accel_mods = Gdk::ModifierType();
        switch(_entry.accel) {
            case GraphicsWindow::DELETE_KEY:
            accel_key = GDK_KEY_Delete;
            break;

            case GraphicsWindow::ESCAPE_KEY:
            accel_key = GDK_KEY_Escape;
            break;

            default:
            accel_key = _entry.accel & ~(GraphicsWindow::SHIFT_MASK | GraphicsWindow::CTRL_MASK);
            if(accel_key > GraphicsWindow::FUNCTION_KEY_BASE &&
                    accel_key <= GraphicsWindow::FUNCTION_KEY_BASE + 12)
                accel_key = GDK_KEY_F1 + (accel_key - GraphicsWindow::FUNCTION_KEY_BASE - 1);
            else
                accel_key = gdk_unicode_to_keyval(accel_key);

            if(_entry.accel & GraphicsWindow::SHIFT_MASK)
                accel_mods |= Gdk::SHIFT_MASK;
            if(_entry.accel & GraphicsWindow::CTRL_MASK)
                accel_mods |= Gdk::CONTROL_MASK;
        }

        MenuItem::set_label(label);
        MenuItem::set_use_underline(true);
        if(!(accel_key & 0x01000000))
            MenuItem::set_accel_key(Gtk::AccelKey(accel_key, accel_mods));
    }

protected:
    virtual void on_activate() {
        if(!MenuItem::has_submenu() && _entry.fn)
            _entry.fn(_entry.id);
    }

private:
    const GraphicsWindow::MenuEntry &_entry;
};

static std::map<int, Gtk::MenuItem *> main_menu_items;

static void InitMainMenu(Gtk::MenuShell *menu_shell) {
    Gtk::MenuItem *menu_item = NULL;
    Gtk::MenuShell *levels[5] = {menu_shell, 0};

    const GraphicsWindow::MenuEntry *entry = &GraphicsWindow::menu[0];
    int current_level = 0;
    while(entry->level >= 0) {
        if(entry->level > current_level) {
            Gtk::Menu *menu = new Gtk::Menu;
            menu_item->set_submenu(*menu);

            if(entry->level >= sizeof(levels) / sizeof(levels[0]))
                oops();

            levels[entry->level] = menu;
        }

        current_level = entry->level;

        if(entry->label) {
            switch(entry->kind) {
                case GraphicsWindow::MENU_ITEM_NORMAL:
                menu_item = new MainMenuItem<Gtk::MenuItem>(*entry);
                break;

                case GraphicsWindow::MENU_ITEM_CHECK:
                menu_item = new MainMenuItem<Gtk::CheckMenuItem>(*entry);
                break;

                case GraphicsWindow::MENU_ITEM_RADIO:
                MainMenuItem<Gtk::CheckMenuItem> *radio_item =
                        new MainMenuItem<Gtk::CheckMenuItem>(*entry);
                radio_item->set_draw_as_radio(true);
                menu_item = radio_item;
                break;
            }
        } else {
            menu_item = new Gtk::SeparatorMenuItem();
        }

        levels[entry->level]->append(*menu_item);

        main_menu_items[entry->id] = menu_item;

        ++entry;
    }
}

void EnableMenuById(int id, bool enabled) {
    static_cast<Gtk::MenuItem*>(main_menu_items[id])->set_sensitive(enabled);
}

void CheckMenuById(int id, bool checked) {
    static_cast<Gtk::CheckMenuItem*>(main_menu_items[id])->
        set_state_flags(checked ? Gtk::STATE_FLAG_CHECKED : Gtk::STATE_FLAG_NORMAL);
}

void RadioMenuById(int id, bool selected) {
    CheckMenuById(id, selected);
}

class RecentMenuItem : public Gtk::MenuItem {
public:
    RecentMenuItem(const Glib::ustring& label, int id) :
            MenuItem(label), _id(id) {
    }

protected:
    virtual void on_activate() {
        if(_id >= RECENT_OPEN && _id < (RECENT_OPEN + MAX_RECENT))
            SolveSpace::MenuFile(_id);
        else if(_id >= RECENT_IMPORT && _id < (RECENT_IMPORT + MAX_RECENT))
            Group::MenuGroup(_id);
    }

private:
    int _id;
};


static void RefreshRecentMenu(int id, int base) {
    Gtk::MenuItem *recent = static_cast<Gtk::MenuItem*>(main_menu_items[id]);
    recent->unset_submenu();

    Gtk::Menu *menu = new Gtk::Menu;
    recent->set_submenu(*menu);

    if(std::string(RecentFile[0]).empty()) {
        Gtk::MenuItem *placeholder = new Gtk::MenuItem("(no recent files)");
        placeholder->set_sensitive(false);
        menu->append(*placeholder);
    } else {
        for(int i = 0; i < MAX_RECENT; i++) {
            if(std::string(RecentFile[i]).empty())
                break;

            RecentMenuItem *item = new RecentMenuItem(RecentFile[i], base + i);
            menu->append(*item);
        }
    }

    menu->show_all();
}

void RefreshRecentMenus(void) {
    RefreshRecentMenu(GraphicsWindow::MNU_OPEN_RECENT, RECENT_OPEN);
    RefreshRecentMenu(GraphicsWindow::MNU_GROUP_RECENT, RECENT_IMPORT);
}

/* Save/load */

static void FiltersFromPattern(const char *active, const char *patterns,
                               Gtk::FileChooser &chooser) {
    Glib::ustring uactive = "*." + Glib::ustring(active);
    Glib::ustring upatterns = patterns;

    Glib::RefPtr<Gtk::FileFilter> filter = Gtk::FileFilter::create();
    Glib::ustring desc = "";
    bool has_name = false, is_active = false;
    int last = 0;
    for(int i = 0; i <= upatterns.length(); i++) {
        if(upatterns[i] == '\t' || upatterns[i] == '\n' || upatterns[i] == '\0') {
            Glib::ustring frag = upatterns.substr(last, i - last);
            if(!has_name) {
                filter->set_name(frag);
                has_name = true;
            } else {
                filter->add_pattern(frag);
                if(uactive == frag)
                    is_active = true;
                if(desc == "")
                    desc = frag;
                else
                    desc += ", " + frag;
            }
        } else continue;

        if(upatterns[i] == '\n' || upatterns[i] == '\0') {
            filter->set_name(filter->get_name() + " (" + desc + ")");
            chooser.add_filter(filter);
            if(is_active)
                chooser.set_filter(filter);

            filter = Gtk::FileFilter::create();
            has_name = false;
            is_active = false;
            desc = "";
        }

        last = i + 1;
    }
}

bool GetOpenFile(char *file, const char *active, const char *patterns) {
    Gtk::FileChooserDialog chooser(*GtkGW, "SolveSpace - Open File");
    chooser.set_filename(file);
    chooser.add_button("_Cancel", Gtk::RESPONSE_CANCEL);
    chooser.add_button("_Open", Gtk::RESPONSE_OK);

    FiltersFromPattern(active, patterns, chooser);

    if(chooser.run() == Gtk::RESPONSE_OK) {
        strcpy(file, chooser.get_filename().c_str());
        return true;
    } else {
        return false;
    }
}

bool GetSaveFile(char *file, const char *active, const char *patterns) {
    Gtk::FileChooserDialog chooser(*GtkGW, "SolveSpace - Save File",
                                   Gtk::FILE_CHOOSER_ACTION_SAVE);
    chooser.set_do_overwrite_confirmation(true);
    chooser.add_button("_Cancel", Gtk::RESPONSE_CANCEL);
    chooser.add_button("_Save", Gtk::RESPONSE_OK);

    chooser.set_filename(file);
    if(chooser.get_current_name() == "")
        chooser.set_current_name(Glib::ustring("untitled.") + active);

    FiltersFromPattern(active, patterns, chooser);

    if(chooser.run() == Gtk::RESPONSE_OK) {
        strcpy(file, chooser.get_filename().c_str());
        return true;
    } else {
        dbp("i");
        return false;
    }
}

int SaveFileYesNoCancel(void) {
    Glib::ustring message =
        "The file has changed since it was last saved.\n"
        "Do you want to save the changes?";
    Gtk::MessageDialog dialog(*GtkGW, message, /*use_markup*/ true, Gtk::MESSAGE_QUESTION,
                              Gtk::BUTTONS_NONE, /*is_modal*/ true);
    dialog.set_title("SolveSpace - Modified File");
    dialog.add_button("_Save", Gtk::RESPONSE_YES);
    dialog.add_button("Do_n't save", Gtk::RESPONSE_NO);
    dialog.add_button("_Cancel", Gtk::RESPONSE_CANCEL);

    switch(dialog.run()) {
        case Gtk::RESPONSE_YES:
        return SAVE_YES;

        case Gtk::RESPONSE_NO:
        return SAVE_NO;

        case Gtk::RESPONSE_CANCEL:
        default:
        return SAVE_CANCEL;
    }
}

/* Text window */

class GtkTextWidget : public GtkGlWidget {
public:
    GtkTextWidget(Glib::RefPtr<Gtk::Adjustment> adjustment) : _adjustment(adjustment) {
        set_events(Gdk::POINTER_MOTION_MASK | Gdk::BUTTON_PRESS_MASK | Gdk::SCROLL_MASK |
                   Gdk::LEAVE_NOTIFY_MASK);
    }

    void set_cursor_hand(bool is_hand) {
        Glib::RefPtr<Gdk::Window> gdkwin = get_window();
        if(gdkwin) { // returns NULL if not realized
            Gdk::CursorType type = is_hand ? Gdk::HAND1 : Gdk::ARROW;
            gdkwin->set_cursor(Gdk::Cursor::create(type));
        }
    }

protected:
    virtual void on_gl_draw() {
        SS.TW.Paint();
    }

    virtual bool on_motion_notify_event(GdkEventMotion *event) {
        SS.TW.MouseEvent(false, event->state & Gdk::BUTTON1_MASK,
                         event->x, event->y);

        return true;
    }

    virtual bool on_button_press_event(GdkEventButton *event) {
        SS.TW.MouseEvent(event->type == Gdk::BUTTON_PRESS,
                         event->state & Gdk::BUTTON1_MASK,
                         event->x, event->y);

        return true;
    }

    virtual bool on_scroll_event(GdkEventScroll *event) {
        int delta_y = event->delta_y;
        if(delta_y == 0) {
            switch(event->direction) {
                case GDK_SCROLL_UP:
                delta_y = -1;
                break;

                case GDK_SCROLL_DOWN:
                delta_y = 1;
                break;

                default:
                /* do nothing */
                return false;
            }
        }

        _adjustment->set_value(_adjustment->get_value() +
                               delta_y * _adjustment->get_page_increment());

        return true;
    }

    virtual bool on_leave_notify_event (GdkEventCrossing*event) {
        SS.TW.MouseLeave();

        return true;
    }

private:
    Glib::RefPtr<Gtk::Adjustment> _adjustment;
};

class GtkTextWindow : public Gtk::Window {
public:
    GtkTextWindow() : _scrollbar(), _widget(_scrollbar.get_adjustment()),
                      _box(), _editor(_widget) {
        set_keep_above(true);
        set_type_hint(Gdk::WINDOW_TYPE_HINT_UTILITY);
        set_skip_taskbar_hint(true);
        set_skip_pager_hint(true);
        set_title("SolveSpace - Browser");

        CnfThawWindowPos(this, "TextWindow");

        _box.pack_start(_editor, true, true);
        _box.pack_start(_scrollbar, false, true);
        add(_box);

        _scrollbar.get_adjustment()->signal_value_changed().
            connect(sigc::mem_fun(this, &GtkTextWindow::on_scrollbar_value_changed));

        _editor.signal_editing_done().
            connect(sigc::mem_fun(this, &GtkTextWindow::on_editing_done));
    }

    Gtk::VScrollbar &get_scrollbar() {
        return _scrollbar;
    }

    GtkTextWidget &get_widget() {
        return _widget;
    }

    GtkEditorOverlay &get_editor() {
        return _editor;
    }

protected:
    virtual void on_hide() {
        CnfFreezeWindowPos(this, "TextWindow");

        Gtk::Window::on_hide();
    }

    virtual void on_scrollbar_value_changed() {
        SS.TW.ScrollbarEvent(_scrollbar.get_adjustment()->get_value());
    }

    virtual void on_editing_done(Glib::ustring value) {
        SS.TW.EditControlDone(value.c_str());
    }

private:
    Gtk::VScrollbar _scrollbar;
    GtkTextWidget _widget;
    GtkEditorOverlay _editor;
    Gtk::HBox _box;
};

GtkTextWindow *GtkTW = NULL;

void ShowTextWindow(bool visible) {
    if(visible)
        GtkTW->show();
    else
        GtkTW->hide();
}

void GetTextWindowSize(int *w, int *h) {
    *w = GtkTW->get_widget().get_allocated_width();
    *h = GtkTW->get_widget().get_allocated_height();
}

void InvalidateText(void) {
    GtkTW->get_widget().queue_draw();
}

void MoveTextScrollbarTo(int pos, int maxPos, int page) {
    GtkTW->get_scrollbar().get_adjustment()->configure(pos, 0, maxPos, 1, 10, page);
}

void SetMousePointerToHand(bool is_hand) {
    GtkTW->get_widget().set_cursor_hand(is_hand);
}

void ShowTextEditControl(int x, int y, char *val) {
    GtkTW->get_editor().start_editing(x, y, val);
}

void HideTextEditControl(void) {
    GtkTW->get_editor().stop_editing();
    GtkGW->raise();
}

bool TextEditControlIsVisible(void) {
    return GtkTW->get_editor().is_editing();
}

/* Miscellanea */


void DoMessageBox(const char *message, int rows, int cols, bool error) {
    Gtk::MessageDialog dialog(*GtkGW, message, /*use_markup*/ true,
                              error ? Gtk::MESSAGE_INFO : Gtk::MESSAGE_ERROR, Gtk::BUTTONS_OK,
                              /*is_modal*/ true);
    dialog.set_title(error ? "SolveSpace - Error" : "SolveSpace - Message");
    dialog.run();
}

void OpenWebsite(const char *url) {
    gtk_show_uri(Gdk::Screen::get_default()->gobj(), url, GDK_CURRENT_TIME, NULL);
}

/* fontconfig is already initialized by GTK */
void LoadAllFontFiles(void) {
    FcPattern   *pat = FcPatternCreate();
    FcObjectSet *os  = FcObjectSetBuild(FC_FILE, (char *)0);
    FcFontSet   *fs  = FcFontList(0, pat, os);

    for(int i = 0; i < fs->nfont; i++) {
        FcChar8 *filename = FcPatternFormat(fs->fonts[i], (const FcChar8*) "%{file}");
        Glib::ustring ufilename = (char*) filename;
        if(ufilename.length() > 4 &&
           ufilename.substr(ufilename.length() - 4, 4).lowercase() == ".ttf") {
            TtfFont tf;
            ZERO(&tf);
            strcpy(tf.fontFile, (char*) filename);
            SS.fonts.l.Add(&tf);
        }
        FcStrFree(filename);
    }

    FcFontSetDestroy(fs);
    FcObjectSetDestroy(os);
    FcPatternDestroy(pat);
}

void ExitNow(void) {
    GtkGW->hide();
    GtkTW->hide();
}

/* Application lifecycle */

class Application : public Gtk::Application {
public:
    Application() : Gtk::Application("com.solvespace", Gio::APPLICATION_HANDLES_OPEN) {
    }

    ~Application() {
        delete GtkGW;
        delete GtkTW;

        SK.Clear();
        SS.Clear();
    }

protected:
    virtual void on_startup() {
        Gtk::Application::on_startup();

        CnfLoad();

        GtkTW = new GtkTextWindow;
        GtkGW = new GtkGraphicsWindow;
        InitMainMenu(&GtkGW->get_menubar());

        add_window(*GtkTW);
        add_window(*GtkGW);

        GtkTW->show_all();
        GtkGW->show_all();
    }

    virtual void on_open(const std::vector< Glib::RefPtr<Gio::File> > &files,
                         const Glib::ustring &hint) {
        Glib::RefPtr<Gio::File> last = files.back();
        SS.Init(last->get_path().c_str());
    }

    virtual void on_activate() {
        SS.Init("");
    }
};

int main(int argc, char** argv) {
    Application app;

    return app.run(argc, argv);
}
