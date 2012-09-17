/*
 * Copyright (C) 2012  Maxim Noah Khailo
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either vedit_refsion 3 of the License, or
 * (at your option) any later vedit_refsion.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <QtGui>

#include "gui/app/lua_script_api.hpp"
#include "gui/util.hpp"
#include "util/uuid.hpp"
#include "util/dbc.hpp"

#include <QTimer>

#include <functional>

namespace m = fire::message;
namespace ms = fire::messages;
namespace us = fire::user;
namespace s = fire::session;
namespace u = fire::util;

namespace fire
{
    namespace gui
    {
        namespace app
        {
            const std::string SIMPLE_MESSAGE = "simple_msg";
            simple_message::simple_message(const std::string& t) : _text{t} { }

            simple_message::simple_message(const m::message& m)
            {
                REQUIRE_EQUAL(m.meta.type, SIMPLE_MESSAGE);
                _text = u::to_str(m.data);
            }
            
            simple_message::operator m::message() const
            {
                m::message m; 
                m.meta.type = SIMPLE_MESSAGE;
                m.data = u::to_bytes(_text);
                return m;
            }

            const std::string& simple_message::text() const
            {
                return _text;
            }

            lua_script_api::lua_script_api(
                    const us::contact_list& con,
                    ms::sender_ptr sndr,
                    s::session_ptr s) :
                contacts{con},
                sender{sndr},
                session{s}
            {
                INVARIANT(sender);
                INVARIANT(session);

                //canvas
                canvas = new QWidget;
                layout = new QGridLayout;
                canvas->setLayout(layout);

                //message list
                output = new list;

                bind();

                INVARIANT(canvas);
                INVARIANT(layout);
                INVARIANT(output);
                INVARIANT(state);
            }

            void lua_script_api::bind()
            {  
                REQUIRE_FALSE(state);

                using namespace std::placeholders;

                SLB::Class<lua_script_api, SLB::Instance::NoCopyNoDestroy>{"Api", &manager}
                    .set("print", &lua_script_api::print)
                    .set("button", &lua_script_api::place_button)
                    .set("edit", &lua_script_api::place_edit)
                    .set("text_edit", &lua_script_api::place_text_edit)
                    .set("total_contacts", &lua_script_api::total_contacts)
                    .set("last_contact", &lua_script_api::last_contact)
                    .set("contact", &lua_script_api::get_contact)
                    .set("when_message_received", &lua_script_api::set_message_callback)
                    .set("send", &lua_script_api::send_all)
                    .set("send_to", &lua_script_api::send_to);

                SLB::Class<contact_ref>{"contact", &manager}
                    .set("name", &contact_ref::get_name)
                    .set("online", &contact_ref::is_online);

                SLB::Class<button_ref>{"button", &manager}
                    .set("get_text", &button_ref::get_text)
                    .set("set_text", &button_ref::set_text)
                    .set("get_callback", &button_ref::get_callback)
                    .set("when_clicked", &button_ref::set_callback)
                    .set("enabled", &widget_ref::enabled)
                    .set("enable", &widget_ref::enable)
                    .set("disable", &widget_ref::disable);

                SLB::Class<edit_ref>{"edit", &manager}
                    .set("get_text", &edit_ref::get_text)
                    .set("set_text", &edit_ref::set_text)
                    .set("get_edited_callback", &edit_ref::get_edited_callback)
                    .set("when_edited", &edit_ref::set_edited_callback)
                    .set("get_finished_callback", &edit_ref::get_finished_callback)
                    .set("when_finished", &edit_ref::set_finished_callback)
                    .set("enabled", &widget_ref::enabled)
                    .set("enable", &widget_ref::enable)
                    .set("disable", &widget_ref::disable);

                SLB::Class<text_edit_ref>{"text_edit", &manager}
                    .set("get_text", &text_edit_ref::get_text)
                    .set("set_text", &text_edit_ref::set_text)
                    .set("get_edited_callback", &text_edit_ref::get_edited_callback)
                    .set("when_edited", &text_edit_ref::set_edited_callback)
                    .set("enabled", &widget_ref::enabled)
                    .set("enable", &widget_ref::enable)
                    .set("disable", &widget_ref::disable);

                state.reset(new SLB::Script{&manager});
                state->set("app", this);

                ENSURE(state);
            }

            QWidget* make_output_widget(const std::string& name, const std::string& text)
            {
                std::string m = "<b>" + name + "</b>: " + text; 
                return new QLabel{m.c_str()};
            }

            std::string lua_script_api::execute(const std::string& s)
                try
                {
                    REQUIRE_FALSE(s.empty());
                    INVARIANT(state);

                    state->doString(s.c_str());
                    return "";
                }
            catch(std::exception& e)
            {
                return e.what();
            }
            catch(...)
            {
                return "unknown";
            }

            void lua_script_api::reset_widgets()
            {
                INVARIANT(output);
                INVARIANT(layout);

                //clear widgets
                QLayoutItem *c = 0;
                while((c = layout->takeAt(0)) != 0)
                {
                    CHECK(c);
                    CHECK(c->widget());

                    delete c->widget();
                    delete c;
                } 

                output->clear();
                button_refs.clear();
                edit_refs.clear();
                text_edit_refs.clear();
                widgets.clear();

                ENSURE_EQUAL(layout->count(), 0);
            }

            void lua_script_api::run(const std::string name, const std::string& code)
            {
                INVARIANT(output);
                REQUIRE_FALSE(code.empty());

                auto error = execute(code);
                if(!error.empty()) output->add(make_output_widget(name, "error: " + error));
            }

            //API implementation 
            template<class W>
                W* get_widget(const std::string& id, widget_map& map)
                {
                    auto wp = map.find(id);
                    return wp != map.end() ? dynamic_cast<W*>(wp->second) : nullptr;
                }

            void set_enabled(const std::string& id, widget_map& map, bool enabled)
            {
                auto w = get_widget<QWidget>(id, map);
                if(!w) return;

                w->setEnabled(enabled);
            }

            void lua_script_api::print(const std::string& a)
            {
                INVARIANT(session);
                INVARIANT(output);
                INVARIANT(session->user_service());

                auto self = session->user_service()->user().info().name();
                output->add(make_output_widget(self, a));
            }

            void lua_script_api::message_recieved(const simple_message& m)
            {
                INVARIANT(state);
                if(message_callback.empty()) return;

                state->call(message_callback, m.text());
            }

            void lua_script_api::set_message_callback(const std::string& a)
            {
                message_callback = a;
            }

            void lua_script_api::send_all(const std::string& m)
            {
                INVARIANT(sender);
                for(auto c : contacts.list())
                {
                    CHECK(c);
                    simple_message sm{m};
                    sender->send(c->id(), sm); 
                }
            }

            void lua_script_api::send_to(const contact_ref& cr, const std::string& m)
            {
                INVARIANT(sender);

                auto c = contacts.by_id(cr.id);
                if(!c) return;

                simple_message sm{m};
                sender->send(c->id(), sm); 
            }

            size_t lua_script_api::total_contacts() const
            {
                return contacts.size();
            }

            int lua_script_api::last_contact() const
            {
                return contacts.size() - 1;
            }

            contact_ref empty_contact_ref(lua_script_api& api)
            {
                contact_ref e;
                e.api = &api;
                e.id = "0";
                return e;
            }

            contact_ref lua_script_api::get_contact(size_t i)
            {
                auto c = contacts.get(i);
                if(!c) return empty_contact_ref(*this);

                contact_ref r;
                r.id = c->id();
                r.api = this;

                ENSURE_EQUAL(r.api, this);
                ENSURE_FALSE(r.id.empty());
                return r;
            }

            std::string contact_ref::get_name() const
            {
                INVARIANT(api);

                auto c = api->contacts.by_id(id);
                if(!c) return "";

                return c->name();
            }

            bool contact_ref::is_online() const
            {
                INVARIANT(api);
                INVARIANT(api->session);

                return api->session->user_service()->contact_available(id);
            }

            bool widget_ref::enabled()
            {
                INVARIANT(api);
                auto w = get_widget<QWidget>(id, api->widgets);
                return w ? w->isEnabled() : false;
            }

            void widget_ref::enable()
            {
                INVARIANT(api);
                set_enabled(id, api->widgets, true);
            }

            void widget_ref::disable()
            {
                INVARIANT(api);
                set_enabled(id, api->widgets, false);
            }

            button_ref lua_script_api::place_button(const std::string& text, int r, int c)
            {
                INVARIANT(layout);
                INVARIANT(canvas);

                //create button reference
                button_ref ref;
                ref.id = u::uuid();
                ref.api = this;

                //create button widget
                auto b = new QPushButton(text.c_str());

                //map button to C++ callback
                auto mapper = new QSignalMapper{canvas};
                mapper->setMapping(b, QString(ref.id.c_str()));
                connect(b, SIGNAL(clicked()), mapper, SLOT(map()));
                connect(mapper, SIGNAL(mapped(QString)), this, SLOT(button_clicked(QString)));

                //add ref and widget to maps
                button_refs[ref.id] = ref;
                widgets[ref.id] = b;

                //place
                layout->addWidget(b, r, c);

                ENSURE_FALSE(ref.id.empty());
                ENSURE(ref.callback.empty());
                ENSURE(ref.api);
                return ref;
            }

            void lua_script_api::button_clicked(QString id)
            {
                INVARIANT(state);

                auto rp = button_refs.find(gui::convert(id));
                if(rp == button_refs.end()) return;

                const auto& callback = rp->second.callback;
                if(callback.empty()) return;

                state->call(callback);
            }

            std::string button_ref::get_text() const
            {
                INVARIANT(api);

                auto rp = api->button_refs.find(id);
                if(rp == api->button_refs.end()) return "";

                auto button = get_widget<QPushButton>(id, api->widgets);
                CHECK(button);

                return gui::convert(button->text());
            }

            void button_ref::set_text(const std::string& t)
            {
                INVARIANT(api);

                auto rp = api->button_refs.find(id);
                if(rp == api->button_refs.end()) return;

                auto button = get_widget<QPushButton>(id, api->widgets);
                CHECK(button);

                button->setText(t.c_str());
            }

            void button_ref::set_callback(const std::string& c)
            {
                INVARIANT(api);

                auto rp = api->button_refs.find(id);
                if(rp == api->button_refs.end()) return;

                rp->second.callback = c;
                callback = c;
            }  

            edit_ref lua_script_api::place_edit(const std::string& text, int r, int c)
            {
                INVARIANT(layout);

                //create edit reference
                edit_ref ref;
                ref.id = u::uuid();
                ref.api = this;

                //create edit widget
                auto e = new QLineEdit(text.c_str());

                //map edit to C++ callback
                auto edit_mapper = new QSignalMapper{canvas};
                edit_mapper->setMapping(e, QString(ref.id.c_str()));
                connect(e, SIGNAL(textChanged(QString)), edit_mapper, SLOT(map()));
                connect(edit_mapper, SIGNAL(mapped(QString)), this, SLOT(edit_edited(QString)));

                auto finished_mapper = new QSignalMapper{canvas};
                finished_mapper->setMapping(e, QString(ref.id.c_str()));
                connect(e, SIGNAL(editingFinished()), finished_mapper, SLOT(map()));
                connect(finished_mapper, SIGNAL(mapped(QString)), this, SLOT(edit_finished(QString)));

                //add ref and widget to maps
                edit_refs[ref.id] = ref;
                widgets[ref.id] = e;

                //place
                layout->addWidget(e, r, c);

                ENSURE_FALSE(ref.id.empty());
                ENSURE(ref.edited_callback.empty());
                ENSURE(ref.finished_callback.empty());
                ENSURE(ref.api);
                return ref;
            }

            void lua_script_api::edit_edited(QString id)
            {
                INVARIANT(state);

                auto rp = edit_refs.find(gui::convert(id));
                if(rp == edit_refs.end()) return;

                const auto& callback = rp->second.edited_callback;
                if(callback.empty()) return;

                state->call(callback);
            }

            void lua_script_api::edit_finished(QString id)
            {
                INVARIANT(state);

                auto rp = edit_refs.find(gui::convert(id));
                if(rp == edit_refs.end()) return;

                const auto& callback = rp->second.finished_callback;
                if(callback.empty()) return;

                state->call(callback);
            }

            std::string edit_ref::get_text() const
            {
                INVARIANT(api);

                auto rp = api->edit_refs.find(id);
                if(rp == api->edit_refs.end()) return "";

                auto edit = get_widget<QLineEdit>(id, api->widgets);
                CHECK(edit);

                return gui::convert(edit->text());
            }

            void edit_ref::set_text(const std::string& t)
            {
                INVARIANT(api);

                auto rp = api->edit_refs.find(id);
                if(rp == api->edit_refs.end()) return;

                auto edit = get_widget<QLineEdit>(id, api->widgets);
                CHECK(edit);

                edit->setText(t.c_str());
            }

            void edit_ref::set_edited_callback(const std::string& c)
            {
                INVARIANT(api);

                auto rp = api->edit_refs.find(id);
                if(rp == api->edit_refs.end()) return;

                rp->second.edited_callback = c;
                edited_callback = c;
            }

            void edit_ref::set_finished_callback(const std::string& c)
            {
                INVARIANT(api);

                auto rp = api->edit_refs.find(id);
                if(rp == api->edit_refs.end()) return;

                rp->second.finished_callback = c;
                finished_callback = c;
            }

            text_edit_ref lua_script_api::place_text_edit(const std::string& text, int r, int c)
            {
                INVARIANT(layout);

                //create edit reference
                text_edit_ref ref;
                ref.id = u::uuid();
                ref.api = this;

                //create edit widget
                auto e = new QTextEdit(text.c_str());

                //map edit to C++ callback
                auto edit_mapper = new QSignalMapper{canvas};
                edit_mapper->setMapping(e, QString(ref.id.c_str()));
                connect(e, SIGNAL(textChanged()), edit_mapper, SLOT(map()));
                connect(edit_mapper, SIGNAL(mapped(QString)), this, SLOT(text_edit_edited(QString)));

                //add ref and widget to maps
                text_edit_refs[ref.id] = ref;
                widgets[ref.id] = e;

                //place
                layout->addWidget(e, r, c);

                ENSURE_FALSE(ref.id.empty());
                ENSURE(ref.edited_callback.empty());
                ENSURE(ref.api);
                return ref;
            }

            void lua_script_api::text_edit_edited(QString id)
            {
                INVARIANT(state);

                auto rp = text_edit_refs.find(gui::convert(id));
                if(rp == text_edit_refs.end()) return;

                const auto& callback = rp->second.edited_callback;
                if(callback.empty()) return;

                state->call(callback);
            }

            std::string text_edit_ref::get_text() const
            {
                INVARIANT(api);

                auto rp = api->text_edit_refs.find(id);
                if(rp == api->text_edit_refs.end()) return "";

                auto edit = get_widget<QTextEdit>(id, api->widgets);
                CHECK(edit);

                return gui::convert(edit->toPlainText());
            }

            void text_edit_ref::set_text(const std::string& t)
            {
                INVARIANT(api);

                auto rp = api->text_edit_refs.find(id);
                if(rp == api->text_edit_refs.end()) return;

                auto edit = get_widget<QTextEdit>(id, api->widgets);
                CHECK(edit);

                edit->setText(t.c_str());
            }

            void text_edit_ref::set_edited_callback(const std::string& c)
            {
                INVARIANT(api);

                auto rp = api->text_edit_refs.find(id);
                if(rp == api->text_edit_refs.end()) return;

                rp->second.edited_callback = c;
                edited_callback = c;
            }
        }
    }
}

