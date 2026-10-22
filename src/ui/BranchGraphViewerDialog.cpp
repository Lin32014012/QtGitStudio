#include "BranchGraphViewerDialog.h"
#include "BranchRelationWidget.h"

#include <QPushButton>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QGraphicsView>
#include <QGraphicsScene>
#include <QWheelEvent>
#include <QShowEvent>
#include <QResizeEvent>
#include <QTimer>
#include <QScrollBar>

BranchGraphViewerDialog::BranchGraphViewerDialog(
    const QString &repositoryPath, QWidget *parent)
    : QDialog(parent) {
    setWindowTitle(QStringLiteral("分支关系图"));
    // v4-P6-21 ISSUE-10: explicit window hints so the viewer can be
    // minimized, maximized and closed like a normal window.
    setWindowFlags(Qt::Window | Qt::WindowMinimizeButtonHint
                   | Qt::WindowMaximizeButtonHint
                   | Qt::WindowCloseButtonHint);
    resize(1080, 720);
    setMinimumSize(720, 480);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(8, 8, 8, 8);
    layout->setSpacing(6);

    auto *toolRow = new QHBoxLayout();
    auto *zoomInBtn = new QPushButton(QStringLiteral("放大"), this);
    auto *zoomOutBtn = new QPushButton(QStringLiteral("缩小"), this);
    auto *fitBtn = new QPushButton(QStringLiteral("查看全部"), this);
    const QString style =
        "QPushButton { background: #FFFFFF; color: #1F2937;"
        " border: 1px solid #D1D5DB; border-radius: 6px;"
        " padding: 4px 10px; font-size: 12px; }"
        "QPushButton:hover { background: #F3F4F6; }";
    for (QPushButton *b : {zoomInBtn, zoomOutBtn, fitBtn}) {
        b->setStyleSheet(style);
    }
    toolRow->addWidget(zoomInBtn);
    toolRow->addWidget(zoomOutBtn);
    toolRow->addWidget(fitBtn);
    toolRow->addStretch();
    layout->addLayout(toolRow);

    m_relation = new BranchRelationWidget(this);
    m_relation->setRepositoryPath(repositoryPath);
    m_relation->refresh();
    m_lastGraphWasLinear = m_relation->usesLinearHistoryLayout();
    // Pan by dragging; the widget's own view gets wheel zoom through
    // the subclassed view installed here.
    layout->addWidget(m_relation, 1);

    connect(m_relation, &BranchRelationWidget::switchBranchRequested,
            this, &BranchGraphViewerDialog::switchBranchRequested);
    connect(m_relation, &BranchRelationWidget::deleteBranchRequested,
            this, &BranchGraphViewerDialog::deleteBranchRequested);
    connect(m_relation, &BranchRelationWidget::mergeBranchRequested,
            this, &BranchGraphViewerDialog::mergeBranchRequested);
    connect(m_relation, &BranchRelationWidget::rebaseBranchRequested,
            this, &BranchGraphViewerDialog::rebaseBranchRequested);
    connect(m_relation, &BranchRelationWidget::graphLayoutRebuilt, this,
            [this]() {
                // A filter may turn a previously fitted multi-lane graph into
                // a timeline. Reconcile only automatic presentation state;
                // explicit user zoom remains authoritative.
                const bool linear = m_relation->usesLinearHistoryLayout();
                const bool transitionedToLinear =
                    !m_lastGraphWasLinear && linear;
                m_lastGraphWasLinear = linear;
                if (!m_userTransformed) {
                    fitGraphToView(transitionedToLinear);
                }
            });

    QGraphicsView *view = m_relation->view();
    if (view) {
        view->setDragMode(QGraphicsView::ScrollHandDrag);
        // v4-P6-18: wheel zoom + drag pan via event filter (no view
        // replacement - keeps BranchRelationWidget internals intact).
        view->viewport()->installEventFilter(this);
    }

    connect(zoomInBtn, &QPushButton::clicked, this, [this, view]() {
        if (view) {
            m_userTransformed = true;
            view->scale(1.15, 1.15);
        }
    });
    connect(zoomOutBtn, &QPushButton::clicked, this, [this, view]() {
        if (view) {
            m_userTransformed = true;
            view->scale(1.0 / 1.15, 1.0 / 1.15);
        }
    });
    connect(fitBtn, &QPushButton::clicked, this, [this]() {
        m_userTransformed = false;
        fitGraphToView(true);
    });
}

void BranchGraphViewerDialog::fitGraphToView(bool resetVerticalPosition) {
    if (!m_relation || !m_relation->view() || !m_relation->scene()) {
        return;
    }
    QGraphicsView *view = m_relation->view();
    if (m_relation->usesLinearHistoryLayout()) {
        // A timeline uses its viewport-relative scene positions at 1:1. Fitting
        // a tall history by height would shrink those positions back into a
        // narrow strip at the left of the Viewer. The relation widget owns the
        // deferred viewport reflow, so this path must not rebuild its scene.
        view->resetTransform();
        view->setAlignment(Qt::AlignLeft | Qt::AlignTop);
        view->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        view->horizontalScrollBar()->setValue(0);
        if (resetVerticalPosition) {
            view->verticalScrollBar()->setValue(
                view->verticalScrollBar()->minimum());
        }
        return;
    }
    const QRectF sceneRect = m_relation->scene()->itemsBoundingRect();
    if (sceneRect.isEmpty()) {
        return;
    }
    view->fitInView(sceneRect.adjusted(-24, -24, 24, 24), Qt::KeepAspectRatio);
}

void BranchGraphViewerDialog::showEvent(QShowEvent *event) {
    QDialog::showEvent(event);
    if (!m_userTransformed) {
        QTimer::singleShot(0, this, [this]() { fitGraphToView(true); });
    }
}

void BranchGraphViewerDialog::resizeEvent(QResizeEvent *event) {
    QDialog::resizeEvent(event);
    if (!m_userTransformed) {
        fitGraphToView();
    }
}

bool BranchGraphViewerDialog::eventFilter(QObject *watched, QEvent *event) {
    if (event->type() == QEvent::Wheel) {
        auto *wheel = static_cast<QWheelEvent *>(event);
        QGraphicsView *view = m_relation->view();
        if (view && watched == view->viewport()) {
            const qreal factor =
                wheel->angleDelta().y() > 0 ? 1.15 : 1.0 / 1.15;
            m_userTransformed = true;
            view->scale(factor, factor);
            return true;
        }
    }
    return QDialog::eventFilter(watched, event);
}
