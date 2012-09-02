/*
 * Copyright (C) 2012  Maxim Noah Khailo
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef FIRESTR_GUI_SESSION_H
#define FIRESTR_GUI_SESSION_H

#include "session/session.hpp"
#include "gui/messagelist.hpp"

#include <QWidget>
#include <QGridLayout>

namespace fire
{
    namespace gui
    {
        class session_widget : public QWidget
        {
            Q_OBJECT
            public:
                session_widget(session::session_ptr);

            public:
                session::session_ptr session();

            public slots:
                void add(message*);

            private:
                QGridLayout* _layout;
                message_list* _messages;
                session::session_ptr _session;
        };
    }
}

#endif