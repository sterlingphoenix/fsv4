#include <gtk/gtk.h>


/* Menu action callbacks (GAction "activate" handlers) */
void
on_file_change_root_activate           (GSimpleAction   *action,
                                        GVariant        *parameter,
                                        gpointer         user_data);

void
on_file_exit_activate                  (GSimpleAction   *action,
                                        GVariant        *parameter,
                                        gpointer         user_data);

/* Vis mode radio action: "change-state" handler */
void
on_vis_mode_change                     (GSimpleAction   *action,
                                        GVariant        *value,
                                        gpointer         user_data);

/* Color mode radio action: "change-state" handler */
void
on_color_mode_change                   (GSimpleAction   *action,
                                        GVariant        *value,
                                        gpointer         user_data);

void
on_color_setup_activate                (GSimpleAction   *action,
                                        GVariant        *parameter,
                                        gpointer         user_data);

void
on_help_about_fsv_activate             (GSimpleAction   *action,
                                        GVariant        *parameter,
                                        gpointer         user_data);

/* Toolbar button callbacks (unchanged — buttons still exist in GTK4) */
void
on_back_button_clicked                 (GtkButton       *button,
                                        gpointer         user_data);

void
on_cd_root_button_clicked              (GtkButton       *button,
                                        gpointer         user_data);

void
on_cd_up_button_clicked                (GtkButton       *button,
                                        gpointer         user_data);

void
on_open_button_clicked                 (GtkButton       *button,
                                        gpointer         user_data);

void
on_birdseye_view_togglebutton_toggled  (GtkToggleButton *togglebutton,
                                        gpointer         user_data);
