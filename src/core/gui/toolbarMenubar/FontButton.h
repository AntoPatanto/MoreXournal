/*
 * Xournal++
 *
 * Part of the customizable toolbars
 *
 * @author Xournal++ Team
 * https://github.com/xournalpp/xournalpp
 *
 * @license GNU GPLv2 or later
 */

#pragma once

#include <string>  // for string

#include <gtk/gtk.h>  // for GtkWidget

#include "control/actions/ActionRef.h"

#include "AbstractToolItem.h"

class ActionDatabase;

class FontButton: public AbstractToolItem {
public:
    FontButton(std::string id, ActionDatabase& db);
    ~FontButton() override = default;

public:
    GtkWidget* getNewToolIcon() const override;
    std::string getToolDisplayName() const override;

    xoj::util::WidgetSPtr createItem(bool horizontal) override;

    /**
     * A button opening the font dialog (win.select-font) that shows the current font
     * @param fontAction The GAction of Action::FONT
     * @return The button, a floating ref
     */
    static GtkWidget* makeButton(GAction* fontAction);

private:
    ActionRef gAction;  ///< Points to the GAction corresponding to Action::FONT
};
