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
#include <gtkmm/application.h>
#include <gtkmm/applicationwindow.h>
#include <gtkmm/label.h>
#include <gdk/gdkx.h>
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

/* Graphics window */

class GtkGraphicsWindow : public Gtk::ApplicationWindow {
public:
    GtkGraphicsWindow() {
        set_reallocate_redraws(true);
        set_events(Gdk::POINTER_MOTION_MASK |
                   Gdk::BUTTON_PRESS_MASK | Gdk::BUTTON_RELEASE_MASK | Gdk::BUTTON_MOTION_MASK |
                   Gdk::SCROLL_MASK);
        set_sensitive(true);
        set_double_buffered(false);

        /* Without this, Gtk will eat the left button presses when
           the window-dragging style property is on (and the window will
           get dragged around). */
        add(_dummy);

        CnfThawWindowPos(this, "GraphicsWindow");

        int attrlist[] = {
            GLX_RGBA,
            GLX_RED_SIZE, 8,
            GLX_GREEN_SIZE, 8,
            GLX_BLUE_SIZE, 8,
            GLX_DEPTH_SIZE, 24,
            GLX_DOUBLEBUFFER,
            0
        };

        _xdisplay = gdk_x11_get_default_xdisplay();
        if(!glXQueryExtension(_xdisplay, NULL, NULL)) {
            dbp("OpenGL is not supported");
            oops();
        }

        XVisualInfo *xvinfo = glXChooseVisual(_xdisplay, gdk_x11_get_default_screen(), attrlist);
        if(!xvinfo) {
            dbp("cannot create glx visual");
            oops();
        }

        _gl = glXCreateContext(_xdisplay, xvinfo, NULL, true);
    }

    bool is_fullscreen() const {
        return _is_fullscreen;
    }

    void set_cursor_hand(bool is_hand) {
        Glib::RefPtr<Gdk::Window> gdkwin = get_window();
        if(gdkwin) { // returns NULL if not realized
            Gdk::CursorType type = is_hand ? Gdk::HAND1 : Gdk::ARROW;
            gdkwin->set_cursor(Gdk::Cursor::create(type));
        }
    }

protected:
    virtual void on_hide() {
        CnfFreezeWindowPos(this, "GraphicsWindow");
    }

    virtual bool on_window_state_event(GdkEventWindowState *event) {
        _is_fullscreen = event->new_window_state & GDK_WINDOW_STATE_FULLSCREEN;

        return true;
    }

    virtual bool on_configure_event(GdkEventConfigure *event) {
        Gtk::ApplicationWindow::on_configure_event(event);

        _w = event->width;
        _h = event->height;

        return true;
    }

    virtual bool on_draw(const Cairo::RefPtr<Cairo::Context> &cr) {
        unsigned long xwindow = gdk_x11_window_get_xid(get_window()->gobj());
        if(!glXMakeCurrent(_xdisplay, xwindow, _gl))
            return false;

        SS.GW.Paint();
        GL_CHECK();

        glXSwapBuffers(_xdisplay, xwindow);

        return true;
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

        SS.GW.MouseScroll(x, y, event->delta_y);

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
    Gtk::Label _dummy;

    Display *_xdisplay;
    GLXContext _gl;

    bool _is_fullscreen;

    int _w, _h;
    void ij_to_xy(int i, int j, int &x, int &y) {
        // Convert to xy (vs. ij) style coordinates,
        // with (0, 0) at center
        x = i - _w / 2;
        y = _h / 2 - j;
    }
};

GtkGraphicsWindow *GtkGW;

void GetGraphicsWindowSize(int *w, int *h) {
    GtkGW->get_size(*w, *h);
}

void InvalidateGraphics(void) {
    GtkGW->get_window()->invalidate(true);
}

void PaintGraphics(void) {
    GtkGW->queue_draw();
}

void SetWindowTitle(const char *str) {
    GtkGW->set_title(str);
}

void SetMousePointerToHand(bool is_hand) {
    GtkGW->set_cursor_hand(is_hand);
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

void ShowGraphicsEditControl(int x, int y, char *s) {
    oops();
}

void HideGraphicsEditControl(void) {
    // oops();
}

bool GraphicsEditControlIsVisible(void) {
    // oops();
    return false;
}

/* Main menu */

void ToggleMenuBar(void) {
    GtkGW->set_show_menubar(!GtkGW->get_show_menubar());
}

bool MenuBarIsVisible(void) {
    return GtkGW->get_show_menubar();
}

void CheckMenuById(int id, bool checked) {
    // oops();
}

void RadioMenuById(int id, bool selected) {
    // oops();
}

void EnableMenuById(int id, bool enabled) {
    // oops();
}

/* Save/load */

bool GetOpenFile(char *file, const char *defExtension, const char *selPattern) {
    oops();
}

bool GetSaveFile(char *file, const char *defExtension, const char *selPattern) {
    oops();
}

int SaveFileYesNoCancel(void) {
    oops();
}

void RefreshRecentMenus(void) {
    // oops();
}

/* Context menus */

void AddContextMenuItem(const char *label, int id) {
    oops();
}

void CreateContextSubmenu(void) {
    oops();
}

int ShowContextMenu(void) {
    oops();
}

/* Text window */

void ShowTextWindow(bool visible) {
    // oops();
}

void InvalidateText(void) {
    // oops();
}

void MoveTextScrollbarTo(int pos, int maxPos, int page) {
    oops();
}

void GetTextWindowSize(int *w, int *h) {
    oops();
}

void ShowTextEditControl(int x, int y, char *s) {
    oops();
}

void HideTextEditControl(void) {
    // oops();
}

bool TextEditControlIsVisible(void) {
    oops();
}

/* Miscellanea */

void DoMessageBox(const char *str, int rows, int cols, bool error) {
    oops();
}

void OpenWebsite(const char *url) {
    oops();
}

void LoadAllFontFiles(void) {
    oops();
}

void ExitNow(void) {
    GtkGW->close();
}

/* Application lifecycle */

class Application : public Gtk::Application {
public:
    Application() : Gtk::Application("com.solvespace", Gio::APPLICATION_HANDLES_OPEN) {
    }

    ~Application() {
        delete GtkGW;
    }

protected:
    virtual void on_startup() {
        Gtk::Application::on_startup();

        CnfLoad();

        GtkGW = new GtkGraphicsWindow;
        add_window(*GtkGW);
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
