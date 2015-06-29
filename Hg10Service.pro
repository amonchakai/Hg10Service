APP_NAME = Hg10Service

CONFIG += qt warn_on cascades
QT += network xml
INCLUDEPATH += src/base /Users/pierre/Documents/Code/Libraries/BB10/swift/3rdParty/Boost/src

INCLUDEPATH += /Users/pierre/Documents/Code/Libraries/libotr/src /Users/pierre/Documents/Code/Libraries/libgcrypt-1.6.2/src /Users/pierre/Documents/Code/Libraries/libgpg-error-1.19/build/x86-qnx/include
LIBS   += -L/Users/pierre/Documents/Code/Libraries/libotr/src/ -lotr -L/Users/pierre/Documents/Code/Libraries/libgcrypt-1.6.2/build/x86-qnx -lgcrypt -L/Users/pierre/Documents/Code/Libraries/libgpg-error-1.19/build/x86-qnx/lib -lintl -lgpg-error


include(config.pri)

LIBS   += -lbbsystem -lbb -lbbpim -lunifieddatasourcec -lbbdata -lbbplatform
