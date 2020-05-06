/*
 *
 * DesktopAboutAlphienDialog.hpp
 *
 * AlphienStudio Version 1.2.1 Copyright (C) Alphien SAS
 * This program is a free software: you can redistribute it and/or modify 
 * it under the terms of the GNU Affero General Public License version 3 
 * as published by the Free Software Foundation (https://www.gnu.org/licenses/agpl-3.0.html). 
 * To request a copy of the software, please contact info@alphien.com.
 *
 * This program comes with ABSOLUTELY NO WARRANTY.
 *
 * AlphienStudio is a modified version of RStudio. Please check RStudio’s notice.
 *
 */

#ifndef DESKTOPABOUTALPHIENDIALOG_HPP
#define DESKTOPABOUTALPHIENDIALOG_HPP

#include <QDialog>

namespace Ui {
    class AboutAlphienDialog;
}

class AboutAlphienDialog : public QDialog
{
    Q_OBJECT

public:
    explicit AboutAlphienDialog(QWidget *parent = 0);
    ~AboutAlphienDialog();

private:
    Ui:: AboutAlphienDialog *ui;
};

#endif // DESKTOPABOUTALPIHENDIALOG_HPP
