#ifndef _QTSCRIPT_QML_ADAPTER_H_
#define _QTSCRIPT_QML_ADAPTER_H_

#include <QCoreApplication>
#include <QDeclarativeView>

namespace QsExecute {

void setupDeclarative(QCoreApplication &app, QDeclarativeView &view);

}

#endif // _QTSCRIPT_QML_ADAPTER_H_