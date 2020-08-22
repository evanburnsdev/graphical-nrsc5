#include <stdio.h>
#include <gtkmm.h>
#include <string>
#include <sstream>
#include <nrsc5.h>
#include <thread>
#include "classes/Nrsc5Helper.h"

#define LOGO_IMG 0
#define COVER_IMG 1

extern Glib::RefPtr<Gdk::Pixbuf> logo_pixbuf;
extern Glib::RefPtr<Gdk::Pixbuf> cover_pixbuf;
extern Gtk::Button *radio_button;
extern Gtk::Entry *freq_text;
extern Gtk::SpinButton *prog_text;
extern Gtk::Entry *artist_text;
extern Gtk::Entry *song_text;
extern Gtk::Entry *album_text;
extern Gtk::Image *logo_img;
extern Gtk::Image *cover_img;
extern Gtk::Image *main_img;
extern Gtk::EventBox *logo_event_box;
extern Gtk::EventBox *cover_event_box;
extern Nrsc5Helper *nh;
Glib::RefPtr<Gdk::Pixbuf> logo_pixbuf;
Glib::RefPtr<Gdk::Pixbuf> cover_pixbuf;
Gtk::Button *radio_button;
Gtk::Entry *freq_text;
Gtk::SpinButton *prog_text;
Gtk::Entry *artist_text;
Gtk::Entry *song_text;
Gtk::Entry *album_text;
Gtk::Image *logo_img;
Gtk::Image *cover_img;
Gtk::Image *main_img;
Gtk::EventBox *logo_event_box;
Gtk::EventBox *cover_event_box;
Nrsc5Helper *nh = new Nrsc5Helper();

extern bool image_selected;
extern bool radio_started;
extern std::string artist;
extern std::string song;
extern std::string album;
bool radio_started = false;
bool image_selected = LOGO_IMG;
std::string artist = "";
std::string song = "";
std::string album = "";

void update_info(){
	unsigned int cover_image_size = 0;
	uint8_t cover_image[100000];
	std::memset(cover_image, 0, 100000*sizeof(uint8_t));
	unsigned int logo_image_size = 0;
	uint8_t logo_image[100000];
	std::memset(logo_image, 0, 100000*sizeof(uint8_t));
	while(radio_started){
		nh->get_track_info(artist, song, album);
		artist_text->set_text(artist);
		song_text->set_text(song);
		album_text->set_text(album);
		nh->get_cover_img(cover_image, cover_image_size);
		if(cover_image_size == 99999){
			cover_pixbuf.clear();
			cover_img->clear();
		} else if(cover_image_size){
			Glib::RefPtr<Gdk::PixbufLoader> loader = Gdk::PixbufLoader::create();
			loader->write(cover_image, cover_image_size);
			loader->close();
			cover_pixbuf = loader->get_pixbuf();
			cover_img->set(cover_pixbuf->scale_simple(30, 30, Gdk::INTERP_BILINEAR));
		}

		nh->get_logo_img(logo_image, logo_image_size);
		if(logo_image_size == 99999){
			logo_pixbuf.clear();
			logo_img->clear();
		} else if(logo_image_size){
			Glib::RefPtr<Gdk::PixbufLoader> loader = Gdk::PixbufLoader::create();
			loader->write(logo_image, logo_image_size);
			loader->close();
			logo_pixbuf = loader->get_pixbuf();
			logo_img->set(logo_pixbuf->scale_simple(30, 30, Gdk::INTERP_BILINEAR));
		}
		if(image_selected == LOGO_IMG){
			main_img->set(logo_pixbuf);
		} else {
			main_img->set(cover_pixbuf);
		}

		sleep(1);
	}
	artist_text->set_text("");
	song_text->set_text("");
	album_text->set_text("");
}

float get_freq(std::string freq_string){
	std::istringstream iss(freq_string);
	float f;
	iss >> std::noskipws >> f;
	if((iss.eof() && !iss.fail()) && (f > 88 && f < 108))
		return f;
	else {
		return -1.0f;
	}
}
void on_prog_text_value_changed(){
	int prog = prog_text->get_value_as_int()-1;
	nh->change_program(prog);
}
void on_radio_button_clicked()
{
	if(radio_button->get_label() == "gtk-media-play"){

		float freq = get_freq(freq_text->get_text());
		if(freq < 0.0f){
			return;
		}
		int prog = prog_text->get_value_as_int();
		if(prog == 10){
			return;
		}
		std::thread nh_thread;
		nh_thread = std::thread(&Nrsc5Helper::start, nh, freq, prog-1);
		nh_thread.detach();
		freq_text->set_editable(false);
		//prog_text->set_editable(false);
		//freq_text->set_can_focus(false);
		freq_text->set_can_focus(false);
		freq_text->set_opacity(0.5);
		//prog_text->set_can_focus(false);
		//prog_text->set_opacity(0.5);
		radio_button->set_label("gtk-stop");
		radio_started = true;
		std::thread label_thread(update_info);
		label_thread.detach();
	} else {
		radio_started = false;
		nh->stop();
		cover_pixbuf.clear();
		logo_pixbuf.clear();
		cover_img->clear();
		logo_img->clear();
		main_img->clear();
		radio_button->set_label("gtk-media-play");
		freq_text->set_editable(true);
		//prog_text->set_editable(true);
		freq_text->set_can_focus(true);
		freq_text->set_opacity(1.0);
		//prog_text->set_can_focus(true);
		//prog_text->set_opacity(1.0);
	}
}
bool on_logo_img_clicked(GdkEventButton*){
	image_selected = LOGO_IMG;
	return true;
}
bool on_cover_img_clicked(GdkEventButton*){
	image_selected = COVER_IMG;
	return true;
}
static void activate()
{
	auto app = Gtk::Application::create("com.evanburnsdev.graphical-nrsc5");
	Glib::RefPtr<Gtk::Builder> builder = Gtk::Builder::create_from_file("gtk.xml");
	Gtk::Window *window;
	//Gtk::Container *fixed1;

	builder->get_widget("main_window", window);
	//builder->get_widget("fixed1", fixed1);
	builder->get_widget("radio_button", radio_button);
	builder->get_widget("freq_text", freq_text);
	builder->get_widget("prog_text", prog_text);
	builder->get_widget("artist_text", artist_text);
	builder->get_widget("song_text", song_text);
	builder->get_widget("album_text", album_text);
	builder->get_widget("logo_img", logo_img);
	builder->get_widget("cover_img", cover_img);
	builder->get_widget("main_img", main_img);
	builder->get_widget("logo_event_box", logo_event_box);
	builder->get_widget("cover_event_box", cover_event_box);

	radio_button->signal_clicked().connect(sigc::ptr_fun(on_radio_button_clicked) );
	prog_text->signal_value_changed().connect(sigc::ptr_fun(on_prog_text_value_changed));
	logo_event_box->set_events(Gdk::BUTTON_PRESS_MASK);
	logo_event_box->signal_button_press_event().connect(sigc::ptr_fun(on_logo_img_clicked));
	cover_event_box->set_events(Gdk::BUTTON_PRESS_MASK);
	cover_event_box->signal_button_press_event().connect(sigc::ptr_fun(on_cover_img_clicked));
	logo_img->set_pixel_size(30);
	cover_img->set_pixel_size(30);
	main_img->set_pixel_size(200);

	app->run(*window);
}

int main()
{
	nh->init();
	//std::thread gtk_thread(activate);
	//gtk_thread.join();
	activate();
	nh->quit();

	return EXIT_SUCCESS;
}
