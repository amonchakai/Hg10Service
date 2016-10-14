APP_NAME = Hg10Service

CONFIG += qt warn_on cascades
QT += network xml
INCLUDEPATH += src/base /Users/pierre/Documents/Code/Libraries/BB10/swift/3rdParty/Boost/src

INCLUDEPATH += /Users/pierre/Documents/Code/Libraries/libotr/src /Users/pierre/Documents/Code/Libraries/libgcrypt-1.6.2/src /Users/pierre/Documents/Code/Libraries/libgpg-error-1.19/build/x86-qnx/include


device {
    CONFIG(debug, debug|release) {
        profile {
            LIBS   += -L/Users/pierre/Documents/Code/Libraries/libotr/build/arm-qnx/ -L/Users/pierre/Documents/Code/Libraries/libgcrypt-1.6.2/build/arm-qnx -L/Users/pierre/Documents/Code/Libraries/libgpg-error-1.19/build/arm-qnx/lib
        } else {
            LIBS   += -L/Users/pierre/Documents/Code/Libraries/libotr/build/arm-qnx/ -L/Users/pierre/Documents/Code/Libraries/libgcrypt-1.6.2/build/arm-qnx -L/Users/pierre/Documents/Code/Libraries/libgpg-error-1.19/build/arm-qnx/lib
        }

    }

    CONFIG(release, debug|release) {
        !profile {
			LIBS   += -L/Users/pierre/Documents/Code/Libraries/libotr/build/arm-qnx -L/Users/pierre/Documents/Code/Libraries/libgcrypt-1.6.2/build/arm-qnx -L/Users/pierre/Documents/Code/Libraries/libgpg-error-1.19/build/arm-qnx/lib
        }
    }
}

simulator {
    CONFIG(debug, debug|release) {
        !profile {
            LIBS   += -L/Users/pierre/Documents/Code/Libraries/libotr/build/x86-qnx -L/Users/pierre/Documents/Code/Libraries/libgcrypt-1.6.2/build/x86-qnx -L/Users/pierre/Documents/Code/Libraries/libgpg-error-1.19/build/x86-qnx/lib
        }
    }
}



include(config.pri)

LIBS   += -lbbsystem -lbb -lbbpim -lunifieddatasourcec -lbbdata -lbbplatform
LIBS   += -lotr  -lgcrypt  -lintl -lgpg-error