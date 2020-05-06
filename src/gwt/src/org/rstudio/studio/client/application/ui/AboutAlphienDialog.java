/*
 *
 * AboutAlphienDialog.java
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
import org.rstudio.core.client.widget.ModalDialogBase;
import org.rstudio.core.client.widget.ThemedButton;
import org.rstudio.studio.client.application.model.ProductInfo;

import com.google.gwt.event.dom.client.ClickEvent;
import com.google.gwt.event.dom.client.ClickHandler;
import com.google.gwt.user.client.ui.Widget;

public class AboutAlphienDialog extends ModalDialogBase
{
   public AboutAlphienDialog(ProductInfo info)
   {
      setText("About AlphienStudio");
      ThemedButton OKButton = new ThemedButton("OK", 
         new ClickHandler() 
      {
            @Override
            public void onClick(ClickEvent event) {
               closeDialog();   
            }
      });
      addOkButton(OKButton);
      contents_ = new AboutAlphienDialogContents(info);
      setWidth("600px");
   }

   @Override
   protected Widget createMainWidget()
   {
      return contents_;
   }
   
   private AboutAlphienDialogContents contents_;
}
