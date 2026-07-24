#pragma once

#include <QDialog>
#include <QDir>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>

/// Reusable folder-selection dialog with path edit + browse button.
/// Returns the selected path, or empty string if cancelled.
inline QString showFolderSelectDialog(QWidget* parent,
                                       const QString& title,
                                       const QString& label,
                                       const QString& defaultPath,
                                       const QString& fileDialogTitle = QStringLiteral("选择目录"))
{
    QDialog dlg(parent);
    dlg.setWindowTitle(title);
    dlg.setMinimumWidth(500);

    auto* vlay = new QVBoxLayout(&dlg);
    vlay->addWidget(new QLabel(label, &dlg));

    auto* hlay = new QHBoxLayout();
    auto* pathEdit = new QLineEdit(defaultPath, &dlg);
    hlay->addWidget(pathEdit);
    auto* browseBtn = new QPushButton(QStringLiteral("浏览..."), &dlg);
    hlay->addWidget(browseBtn);
    vlay->addLayout(hlay);

    auto* btnLayout = new QHBoxLayout();
    btnLayout->addStretch();
    auto* okBtn = new QPushButton(QStringLiteral("确认"), &dlg);
    auto* cancelBtn = new QPushButton(QStringLiteral("取消"), &dlg);
    btnLayout->addWidget(okBtn);
    btnLayout->addWidget(cancelBtn);
    vlay->addLayout(btnLayout);

    QObject::connect(browseBtn, &QPushButton::clicked, [&]() {
        QString dir = QFileDialog::getExistingDirectory(&dlg, fileDialogTitle,
                                                         pathEdit->text(),
                                                         QFileDialog::ShowDirsOnly);
        if (!dir.isEmpty()) pathEdit->setText(dir);
    });
    QObject::connect(okBtn, &QPushButton::clicked, &dlg, &QDialog::accept);
    QObject::connect(cancelBtn, &QPushButton::clicked, &dlg, &QDialog::reject);

    if (dlg.exec() == QDialog::Accepted && !pathEdit->text().isEmpty())
        return pathEdit->text();
    return {};
}
