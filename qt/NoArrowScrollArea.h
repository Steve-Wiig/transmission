#pragma once

#include <QScrollArea>
#include <QKeyEvent>
#include <QApplication>
#include <QAbstractSpinBox>

class NoArrowScrollArea : public QScrollArea
{
public:
    explicit NoArrowScrollArea(QWidget* parent = nullptr) 
        : QScrollArea(parent) 
    {
    }

protected:
    void keyPressEvent(QKeyEvent* event) override
    {
        int const key = event->key();
        bool const is_vertical_nav = (
            key == Qt::Key_Up || 
            key == Qt::Key_Down || 
            key == Qt::Key_PageUp || 
            key == Qt::Key_PageDown
        );

        if (is_vertical_nav)
        {
            QWidget* focus_widget = QApplication::focusWidget();
            
            // If a widget inside this scroll area has focus, and it's a spinbox (or its child),
            // it means the spinbox ignored the key (e.g., hit min/max limit).
            // We must consume the event here to prevent the scroll area from scrolling.
            if (focus_widget && this->isAncestorOf(focus_widget))
            {
                // Walk up the parent chain to verify it's actually a spinbox
                QWidget* w = focus_widget;
                bool is_spinbox_child = false;
                while (w) {
                    if (qobject_cast<QAbstractSpinBox*>(w)) {
                        is_spinbox_child = true;
                        break;
                    }
                    w = w->parentWidget();
                }

                if (is_spinbox_child) {
                    event->accept(); // Consume the ignored event. Do NOT scroll.
                    return;
                }
            }
        }

        // For all other keys, or if focus is outside our spinboxes, use default scrolling behavior
        QScrollArea::keyPressEvent(event);
    }
};