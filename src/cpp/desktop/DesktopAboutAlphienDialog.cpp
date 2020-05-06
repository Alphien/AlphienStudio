/*
 *
 * DesktopAboutAlphienDialog.cpp
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

#include "DesktopAboutAlphienDialog.hpp"
#include "ui_DesktopAboutAlphienDialog.h"

#include <core/FilePath.hpp>
#include <core/FileSerializer.hpp>
#include <core/system/System.hpp>

#include <QPushButton>

#include "DesktopOptions.hpp"
#include "desktop-config.h"

using namespace rstudio::core;
using namespace rstudio::desktop;

AboutAlphienDialog::AboutAlphienDialog(QWidget *parent) :
      QDialog(parent, Qt::Dialog),
      ui(new Ui::AboutAlphienDialog())
{
   ui->setupUi(this);

   ui->buttonBox->addButton(new QPushButton(QString::fromUtf8("OK")),
                            QDialogButtonBox::AcceptRole);
   ui->lblIcon->setPixmap(QPixmap(QString::fromUtf8(":/icons/resources/freedesktop/icons/64x64/rstudio.png")));
   ui->lblVersion->setText(QString::fromUtf8(
             "Version " RSTUDIO_VERSION " - © 2009-2012 RStudio, Inc."));

   setWindowModality(Qt::ApplicationModal);

   // read notice file
   FilePath supportingFilePath = options().supportingFilePath();
   FilePath noticePath = supportingFilePath.complete("NOTICE");
   std::string notice;
   Error error = readStringFromFile(noticePath, &notice);
   if (!error)
   {
      ui->textBrowser->setFontFamily(options().fixedWidthFont());
#ifdef Q_OS_MAC
      ui->textBrowser->setFontPointSize(11);
#else
      ui->textBrowser->setFontPointSize(9);
#endif
      ui->textBrowser->setText(QString::fromUtf8(notice.c_str()));
   }
}

AboutAlphienDialog::~AboutAlphienDialog()
{
   delete ui;
}
