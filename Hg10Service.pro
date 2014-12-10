APP_NAME = Hg10Service

CONFIG += qt warn_on cascades
QT += network xml
INCLUDEPATH += src/base /Users/pierre/Documents/Code/Libraries/BB10/swift/3rdParty/Boost/src

include(config.pri)

LIBS   += -lbbsystem -lbb -lbbpim -lunifieddatasourcec -lbbdata -lbbplatform
