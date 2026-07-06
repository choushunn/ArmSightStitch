#include <QApplication>
#include "gui/MainWindow.h"

int main(int argc, char *argv[]) {
    QApplication a(argc, argv);
    
    // Set application properties
    a.setApplicationName("ArmLite C++ Control System");
    a.setApplicationVersion("1.0.0");
    a.setOrganizationName("ArmLite");
    
    // Create main window
    MainWindow w;
    w.show();
    
    // Run event loop
    return a.exec();
}
