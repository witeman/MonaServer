/*
Copyright 2014 Mona
mathieu.poux[a]gmail.com
jammetthomas[a]gmail.com

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License received along this program for more
details (or else see http://www.gnu.org/licenses/).

This file is a part of Mona.
*/

#include "Service.h"
#include "Mona/String.h"
#include "Mona/Logs.h"


using namespace std;
using namespace Mona;

Service::Service(lua_State* pState, ServiceHandler& handler) : _lastCheck(0), _reference(LUA_REFNIL), _pParent(NULL), _handler(handler), _pState(pState), FileWatcher(handler.wwwPath(),"main.lua") {

}

Service::Service(lua_State* pState, Service& parent, const string& name, ServiceHandler& handler) : _lastCheck(0), _reference(LUA_REFNIL), _pParent(&parent), _handler(handler), _pState(pState), FileWatcher(handler.wwwPath(),parent.path,"/",name,"main.lua") {
	String::Format((string&)path,parent.path,"/",name);
}

Service::~Service() {
	// clean children
	for (auto& it : _services)
		delete it.second;
	// clean this
	close(true);
}

void Service::setReference(int reference) {
	if (reference == _reference)
		return;
	// make obsolete the connected clients
	if (_reference != LUA_REFNIL) {
		lua_rawgeti(_pState, LUA_REGISTRYINDEX, _reference);
		Script::Collection(_pState, -1, "clients");
		bool isConst;
		lua_pushnil(_pState);  // first key 
		while (lua_next(_pState, -2) != 0) {
			// uses 'key' (at index -2) and 'value' (at index -1) 
			Client* pClient(Script::ToObject<Client>(_pState, isConst));
			if (pClient)
				pClient->data(LUA_REFNIL);
			lua_pop(_pState, 1);
		}
		lua_pop(_pState, 2);
		luaL_unref(_pState, LUA_REGISTRYINDEX, _reference);
	}
	_reference = reference;
}

Service* Service::open(Exception& ex) {
	if (_lastCheck.isElapsed(2000)) { // already checked there is less of 2 sec!
		_lastCheck.update();
		if (!watchFile() && !Mona::FileSystem::Exists(filePath.directory()))
			_ex.set(Exception::APPLICATION, "Applicaton ", path, " doesn't exist").error();
	}
	
	if (_ex) {
		ex.set(_ex);
		return NULL;
	}

	// here => exists and no error on load
	open(true);
	return this;
}

Service* Service::open(Exception& ex, const string& path) {
	// remove first '/'
	string name;
	if(!path.empty())
		name.assign(path[0] == '/' ? &path.c_str()[1] : path.c_str());

	// substr first "service"
	size_t pos = name.find('/');
	string nextPath;
	if (pos != string::npos) {
		nextPath = &name.c_str()[pos];
		name.resize(pos);
	}

	Service* pSubService(this);
	auto it = _services.end();
	if (!name.empty()) {
		it = _services.lower_bound(name);
		if (it == _services.end() || it->first != name)
			it = _services.emplace_hint(it, name, new Service(_pState, *this, name, _handler)); // Service doesn't exists
		pSubService = it->second;
	}

	if (!nextPath.empty())
		return pSubService->open(ex,nextPath);

	 // if file or folder exists, return the service
	if (pSubService->open(ex))
		return pSubService;

	// service doesn't exist (and no children possible here!)
	if (it != _services.end() && _ex.code() == Exception::APPLICATION) {
		delete it->second;
		_services.erase(it);
	}
	return NULL;
}

bool Service::open(bool create) {
	if (_reference != LUA_REFNIL)
		return true;
	if (!create)
		return false;

	//// create environment

	// table environment
	lua_newtable(_pState);

	// metatable
	lua_newtable(_pState);

#if !defined(_DEBUG)
	// hide metatable
	lua_pushstring(_pState, "change metatable of environment is prohibited");
	lua_setfield(_pState, -2, "__metatable");
#endif

	// set parent
	if (_pParent) {
		_pParent->open(true); // guarantee the creation of parent!
		lua_rawgeti(_pState, LUA_REGISTRYINDEX, _pParent->reference());
		// fill children of parent!
		Script::Collection(_pState,-1,"children");
		lua_pushstring(_pState, name.c_str());
		lua_pushvalue(_pState, -5);
		Script::FillCollection(_pState, 1);
		lua_pop(_pState, 1); // remove children collection
	} else
		lua_pushvalue(_pState, LUA_GLOBALSINDEX);
	lua_setfield(_pState,-2,"super");

	// set name
	lua_pushstring(_pState, name.c_str());
	lua_setfield(_pState, -2, "name");

	// set path
	lua_pushstring(_pState, path.c_str());
	lua_setfield(_pState, -2, "path");

	// set this
	lua_pushvalue(_pState,-2);
	lua_setfield(_pState, -2, "this");

	// set __index=Service::Index
	lua_pushcfunction(_pState,&Service::Index);
	lua_setfield(_pState,-2,"__index");

	// set metatable
	lua_setmetatable(_pState,-2);

	// create children collection (collector required here!)
	Script::Collection<Service>(_pState,-1,"children",this);
	lua_pop(_pState, 1);

	// record in registry
	setReference(luaL_ref(_pState, LUA_REGISTRYINDEX));

	return true;
}

void Service::loadFile() {
	if (_ex)
		return;

	open(true);
	
	_ex.set(Exception::NIL);

	SCRIPT_BEGIN(_pState)

		lua_rawgeti(_pState, LUA_REGISTRYINDEX, _reference);
		if(luaL_loadfile(_pState,filePath.fullPath().c_str())!=0) {
			const char* error = Script::LastError(_pState);
			SCRIPT_ERROR(error)
			_ex.set(Exception::SOFTWARE, error);
			lua_pop(_pState,1); // remove environment
			return;
		}

		lua_pushvalue(_pState, -2);
		lua_setfenv(_pState, -2);
		if(lua_pcall(_pState, 0,0, 0)==0) {
			SCRIPT_FUNCTION_BEGIN("onStart",_reference)
				SCRIPT_WRITE_STRING(path.c_str())
				SCRIPT_FUNCTION_CALL
			SCRIPT_FUNCTION_END
			_handler.startService(*this);
			SCRIPT_INFO("Application www", path, " loaded")
		} else {
			_ex.set(Exception::SOFTWARE, Script::LastError(_pState));
			SCRIPT_ERROR(_ex.error());
			clearEnvironment();
		}

		lua_pop(_pState, 1);
	SCRIPT_END
}

void Service::close(bool full) {

	if (open(false)) {

		if (!_ex) { // loaded!
			_handler.stopService(*this);
			SCRIPT_BEGIN(_pState)
				SCRIPT_FUNCTION_BEGIN("onStop",_reference)
					SCRIPT_WRITE_STRING(path.c_str())
					SCRIPT_FUNCTION_CALL
				SCRIPT_FUNCTION_END
			SCRIPT_END
		}

		lua_rawgeti(_pState, LUA_REGISTRYINDEX, _reference);
		if (full) {
			// Delete environment
			if (lua_getmetatable(_pState, -1)) {
				lua_getfield(_pState, -1, "super");
				if (lua_istable(_pState, -1)) {
					Script::Collection(_pState, -1, "children");
					lua_pushstring(_pState, name.c_str());
					lua_pushnil(_pState);
					Script::FillCollection(_pState, 1);
					lua_pop(_pState, 1);
				}
				lua_pop(_pState, 2);
			}
			setReference(LUA_REFNIL);
		} else
			clearEnvironment();
		lua_pop(_pState, 1);

		lua_gc(_pState, LUA_GCCOLLECT, 0);
	}

	_ex.set(Exception::NIL);
}

void Service::clearEnvironment() {
	// Clear environment
	lua_pushnil(_pState);  // first key 
	while (lua_next(_pState, -2) != 0) {
		// uses 'key' (at index -2) and 'value' (at index -1) 
		// remove the raw!
		lua_pushvalue(_pState, -2); // duplicate key
		lua_pushnil(_pState);
		lua_rawset(_pState, -5);
		lua_pop(_pState, 1);
	}
}





int Service::Item(lua_State *pState) {
	// 1 => children table
	// 2 => key not found
	// here it check the existing of the service
	if (!lua_isstring(pState,2))
		return 0;
	string name(lua_tostring(pState, 2));
	if (name.empty())
		return 0;
	Service* pService = Script::GetCollector<Service>(pState,1);
	Exception ex;
	if (!pService || !(pService=pService->open(ex, name)))
		return 0;
	lua_rawgeti(pState, LUA_REGISTRYINDEX, pService->reference());
	return 1;
}


// Call when a key is not available in the service table
int Service::Index(lua_State *pState) {
	if (!lua_getmetatable(pState, 1))
		return 0;

	if (lua_isstring(pState, 2)) {
		const char* key = lua_tostring(pState, 2);
		
		// |data table request?
		if (strcmp(key, "data") == 0) {
			lua_getfield(pState, LUA_REGISTRYINDEX, "|data");
			if (lua_istable(pState,-1)) {
				lua_replace(pState, -2); // replace first metatable

				string path;
				lua_getfield(pState, 1, "path");
				if (lua_isstring(pState, -1))
					path.assign(lua_tostring(pState, -1));
				lua_pop(pState, 1);


				if (!path.empty()) {  // else return |data
					String::ForEach forEach([pState](const char* value){
						lua_getfield(pState,-1, value);
						if (lua_isnil(pState, -1)) {
							lua_pop(pState, 1);
							lua_newtable(pState);
							lua_pushvalue(pState,1);
							lua_setfield(pState, -3, value);
						}
						lua_replace(pState, -2);
					});
					String::Split(path, "/", forEach,String::SPLIT_IGNORE_EMPTY | String::SPLIT_TRIM);
				}

				// set data for the application!
				lua_pushstring(pState,"data");
				lua_pushvalue(pState, -2);
				lua_rawset(pState, 1);
				return 1;
			}
			lua_pop(pState, 1);
		}

		// search in metatable (contains super, children, path, name, this, clients, ...)
		lua_getfield(pState, -1, key);
		if (!lua_isnil(pState, -1)) {
			lua_replace(pState, -2);
			// recort to accelerate the access
			lua_pushstring(pState,key);
			lua_pushvalue(pState, -2);
			lua_rawset(pState, 1);
			return 1;
		}
		lua_pop(pState, 1);
	}
	
	// search in parent (inheriting)
	lua_getfield(pState, -1, "super");
	lua_replace(pState,-2);
	if (lua_isnil(pState, -1))
		return 1; // no parent (returns nil)

	lua_pushvalue(pState, 2);
	lua_gettable(pState,-2);
	lua_replace(pState,-2); // replace parent by result
	return 1;
}
