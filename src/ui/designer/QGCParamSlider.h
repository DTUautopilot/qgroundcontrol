#ifndef QGCPARAMSLIDER_H
#define QGCPARAMSLIDER_H

#include <QWidget>
#include <QAction>
#include <QtDesigner/QDesignerExportWidget>

#include "QGCToolWidgetItem.h"

namespace Ui {
    class QGCParamSlider;
}

class QGCParamSlider : public QGCToolWidgetItem
{
    Q_OBJECT

public:
    explicit QGCParamSlider(QWidget *parent = 0);
    ~QGCParamSlider();

public slots:
    void startEditMode();
    void endEditMode();
    /** @brief Send the parameter to the MAV */
    void sendParameter();
    /** @brief Set the slider value as parameter value */
    void setSliderValue(int sliderValue);
    /** @brief Update the UI with the new parameter value */
    void setParameterValue(int uas, int component, int paramCount, int paramIndex, QString parameterName, float value);
    void writeSettings(QSettings& settings);
    void readSettings(const QSettings& settings);
    void refreshParamList();
    void setActiveUAS(UASInterface *uas);
    void selectComponent(int componentIndex);
    void selectParameter(int paramIndex);
    void setParamValue(double value);

protected slots:
    /** @brief Request the parameter of this widget from the MAV */
    void requestParameter();

protected:
    QString parameterName;         ///< Key/Name of the parameter
    float parameterValue;          ///< Value of the parameter
    double parameterScalingFactor; ///< Factor to scale the parameter between slider and true value
    float parameterMin;
    float parameterMax;
    int component;                 ///< ID of the MAV component to address
    int parameterIndex;
    double scaledInt;
    void changeEvent(QEvent *e);

    /** @brief Convert scaled int to float */

    float scaledIntToFloat(int sliderValue);
    int floatToScaledInt(float value);

private:
    Ui::QGCParamSlider *ui;
};

#endif // QGCPARAMSLIDER_H
