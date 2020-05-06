/*
 *
 * AboutAlphienDialogContents.java
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

package org.rstudio.studio.client.application.ui;

import org.rstudio.studio.client.application.model.ProductInfo;

import com.google.gwt.core.client.GWT;
import com.google.gwt.uibinder.client.UiBinder;
import com.google.gwt.uibinder.client.UiField;
import com.google.gwt.user.client.Window;
import com.google.gwt.user.client.ui.Composite;
import com.google.gwt.user.client.ui.InlineLabel;
import com.google.gwt.user.client.ui.TextArea;
import com.google.gwt.user.client.ui.Widget;

public class AboutAlphienDialogContents extends Composite
{
   public static void ensureStylesInjected()
   {
      new AboutAlphienDialogContents();
   }
   
   private static AboutAlphienDialogContentsUiBinder uiBinder = GWT
         .create(AboutAlphienDialogContentsUiBinder.class);

   interface AboutAlphienDialogContentsUiBinder extends
         UiBinder<Widget, AboutAlphienDialogContents>
   {
   }
   
   private AboutAlphienDialogContents()
   {
      uiBinder.createAndBindUi(this);
   }

   public AboutAlphienDialogContents(ProductInfo info)
   {
      initWidget(uiBinder.createAndBindUi(this));
      // versionLabel.setText(info.getVersion());
      userAgentLabel.setText(Window.Navigator.getUserAgent());
      // noticeBox.setValue(info.getNotice());
   }

   // @UiField InlineLabel versionLabel;
   @UiField InlineLabel userAgentLabel;
   // @UiField TextArea noticeBox;
}
