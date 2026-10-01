#include <FL/Fl.H>
#include <FL/Fl_Double_Window.H>

#include "view/MainWindow.h"

int main(int argc, char** argv) {
    Fl::visual(FL_RGB);
    lab2::MainWindow window;
    window.show(argc, argv);
    return Fl::run();
}