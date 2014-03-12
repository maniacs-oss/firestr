/*
 * Copyright (C) 2014  Maxim Noah Khailo
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


#include "gui/conversation.hpp"
#include "gui/unknown_message.hpp"
#include "gui/util.hpp"

#include "util/log.hpp"

#include <QtWidgets>

namespace s = fire::conversation;
namespace m = fire::message;
namespace ms = fire::messages;
namespace us = fire::user;
namespace a = fire::gui::app;

namespace fire
{
    namespace gui
    {
        namespace 
        {
            const size_t TIMER_SLEEP = 200;//in milliseconds
            const size_t ADD_CONTACT_WIDTH = 10;
        }

        conversation_widget::conversation_widget(
                s::conversation_service_ptr conversation_service,
                s::conversation_ptr conversation,
                a::app_service_ptr app_service) :
            _conversation_service{conversation_service},
            _conversation{conversation},
            _app_service{app_service},
            _messages{new message_list{app_service, conversation_service, conversation}}
        {
            REQUIRE(conversation_service);
            REQUIRE(conversation);
            REQUIRE(app_service);

            _layout = new QGridLayout;

            _contact_select = new contact_select_widget{conversation_service->user_service(), 
                [conversation](const us::user_info& u) -> bool
                {
                    return !conversation->contacts().by_id(u.id());
                }
            };

            _add_contact = new QPushButton{"+"};
            _add_contact->setMaximumSize(20,20);
            connect(_add_contact, SIGNAL(clicked()), this, SLOT(add_contact()));

            update_contact_select();

            auto* cw = new QWidget;
            auto* cl = new QGridLayout;

            cw->setLayout(cl);
            cl->addWidget(_contact_select, 0,0);
            cl->addWidget(_add_contact, 0, 1);
            cl->addWidget(_messages, 1, 0, 1, 3);

            _layout->addWidget(cw);

            setLayout(_layout);
            _layout->setContentsMargins(0,0,0,0);

            //setup mail timer
            auto *t2 = new QTimer(this);
            connect(t2, SIGNAL(timeout()), this, SLOT(check_mail()));
            t2->start(TIMER_SLEEP);

            INVARIANT(_conversation_service);
            INVARIANT(_conversation);
            INVARIANT(_messages);
            INVARIANT(_layout);
            INVARIANT(_app_service);
        }
        
        void conversation_widget::update_contact_select()
        {
            INVARIANT(_contact_select);
            _contact_select->update_contacts();

            bool enabled = _contact_select->count() > 0;
            _add_contact->setEnabled(enabled);
        }

        void conversation_widget::add(message* m)
        {
            REQUIRE(m);
            INVARIANT(_messages);
            _messages->add(m);
        }

        void conversation_widget::add(QWidget* w)
        {
            REQUIRE(w);
            INVARIANT(_messages);
            _messages->add(w);
        }

        s::conversation_ptr conversation_widget::conversation()
        {
            ENSURE(_conversation);
            return _conversation;
        }

        QWidget* contact_alert(us::user_info_ptr c, const std::string message)
        {
            REQUIRE(c);

            auto w = new QWidget;
            auto l = new QHBoxLayout;
            w->setLayout(l);

            std::stringstream s;
            s << "<b>" << c->name() << "</b> " << message;

            auto t = new QLabel{s.str().c_str()};
            l->addWidget(t);

            ENSURE(w);
            return w;
        }

        void conversation_widget::add_contact()
        {
            INVARIANT(_contact_select);
            INVARIANT(_conversation_service);
            INVARIANT(_conversation);

            auto contact = _contact_select->selected_contact();
            if(!contact) return;

            _conversation_service->add_contact_to_conversation(contact, _conversation);

            add(contact_alert(contact, convert(tr("added to conversation"))));
            update_contacts();
        }

        void conversation_widget::update_contacts()
        {
            _messages->update_contact_lists();
            update_contact_select();
        }

        void conversation_widget::name(const QString& s)
        {
            _name = s;
        }

        QString conversation_widget::name() const
        {
            return _name;
        }

        void conversation_widget::check_mail()
        try
        {
            INVARIANT(_messages);
            INVARIANT(_conversation);
            INVARIANT(_conversation_service);
            INVARIANT(_conversation_service->user_service());
            INVARIANT(_conversation->mail());

            m::message m;
            while(_conversation->mail()->pop_inbox(m))
            {
                if(m.meta.type == ms::NEW_APP)
                {
                    m::expect_symmetric(m);

                    auto id = _messages->add_new_app(m);
                    _conversation->add_app_id(id);
                    _conversation_service->fire_conversation_alert(_conversation->id());
                }
                else if(m.meta.type == s::event::CONVERSATION_SYNCED)
                {
                    update_contacts();
                    _conversation_service->fire_conversation_alert(_conversation->id());
                }
                else if(m.meta.type == s::event::CONTACT_REMOVED)
                {
                    s::event::contact_removed r;
                    s::event::convert(m, r);

                    if(r.conversation_id != _conversation->id()) continue;

                    auto c = _conversation_service->user_service()->by_id(r.contact_id);
                    if(!c) continue;

                    add(contact_alert(c, convert(tr("quit conversation"))));

                    _messages->remove_from_contact_lists(c);
                    update_contacts();
                    _conversation_service->fire_conversation_alert(_conversation->id());
                }
                else if(m.meta.type == s::event::CONTACT_ADDED)
                {
                    s::event::contact_added r;
                    s::event::convert(m, r);

                    if(r.conversation_id != _conversation->id()) continue;

                    auto c = _conversation_service->user_service()->by_id(r.contact_id);
                    if(!c) continue;

                    add(contact_alert(c, convert(tr("added to conversation"))));
                    update_contacts();
                    _conversation_service->fire_conversation_alert(_conversation->id());
                }
                else if(m.meta.type == us::event::CONTACT_CONNECTED)
                {
                    update_contacts();
                }
                else if(m.meta.type == us::event::CONTACT_DISCONNECTED)
                {
                    us::event::contact_disconnected r;
                    us::event::convert(m, r);

                    auto c = _conversation->contacts().by_id(r.id);
                    if(!c) continue;

                    update_contacts();
                }
                else
                {
                    std::stringstream s;
                    s << m;

                    _messages->add(new unknown_message{s.str()});
                }
            }
        }
        catch(std::exception& e)
        {
            LOG << "conversation: error in check_mail. " << e.what() << std::endl;
        }
        catch(...)
        {
            LOG << "conversation: unexpected error in check_mail." << std::endl;
        }
    }
}

