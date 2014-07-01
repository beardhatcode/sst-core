// Copyright 2009-2014 Sandia Corporation. Under the terms
// of Contract DE-AC04-94AL85000 with Sandia Corporation, the U.S.
// Government retains certain rights in this software.
// 
// Copyright (c) 2009-2014, Sandia Corporation
// All rights reserved.
// 
// This file is part of the SST software package. For license
// information, see the LICENSE file in the top level directory of the
// distribution.


#include <sst_config.h>
#include "sst/core/serialization.h"
#include <sst/core/configGraph.h>

#include <fstream>
#include <boost/format.hpp>

#include <sst/core/config.h>
#include <sst/core/timeLord.h>
#include <sst/core/simulation.h>

#include <string.h>
#include <set>

using namespace std;

namespace SST {

#define _GRAPH_DBG( fmt, args...) __DBG( DBG_GRAPH, Graph, fmt, ## args )

void ConfigComponent::print(std::ostream &os) const {
    os << "Component " << name << " (id = " << id << ")" << std::endl;
    os << "  type = " << type << std::endl;
    os << "  weight = " << weight << std::endl;
    os << "  rank = " << rank << std::endl;
    os << "  isIntrospector = " << isIntrospector << std::endl;
    os << "  Links:" << std::endl;
    // for (size_t i = 0 ; i != links.size() ; ++i) {
    //     links[i]->print(os);
    // }

    os << "  Params:" << std::endl;
    params.print_all_params(os);

}

void ConfigComponent::genDot(std::ostream &os) const {
    os  << id
        << " [label=\"{" << name << "\\n" << type << " | {";
    // for ( std::vector<ConfigLink*>::const_iterator i = links.begin() ; i != links.end() ; ++i ) {
    //     // Choose which side of the link we're on
    //     int p = ((*i)->component[0] == id) ? 0 : 1;
    //     os << " <" << (*i)->port[p] << "> " << (*i)->port[p];
    //     if ( i+1 != links.end() ) os << " |";
    // }
    os << " } }\"];\n";
}

ConfigComponent
ConfigComponent::cloneWithoutLinks() const
{
    ConfigComponent ret;
    ret.id = id;
    ret.name = name;
    ret.type = type;
    ret.weight = weight;
    ret.rank = rank;
    ret.params = params;
    ret.isIntrospector = isIntrospector;
    return ret;
}
    
ConfigComponent
ConfigComponent::cloneWithoutLinksOrParams() const
{
    ConfigComponent ret;
    ret.id = id;
    ret.name = name;
    ret.type = type;
    ret.weight = weight;
    ret.rank = rank;
    ret.isIntrospector = isIntrospector;
    return ret;
}
    
void
ConfigGraph::setComponentRanks(int rank)
{
    for ( ConfigComponentMap_t::iterator iter = comps.begin();
                            iter != comps.end(); ++iter )
    {
        iter->rank = rank;
    }

}

bool
ConfigGraph::containsComponentInRank(int rank)
{
    for ( ConfigComponentMap_t::iterator iter = comps.begin();
                            iter != comps.end(); ++iter )
    {
        if ( iter->rank == rank ) return true;
    }
    return false;

}

bool
ConfigGraph::checkRanks(int ranks)
{
    for ( ConfigComponentMap_t::iterator iter = comps.begin();
                            iter != comps.end(); ++iter )
    {
        int rank = iter->rank;
        if ( rank < 0 || rank >= ranks ) {
            return false;
        }
    }
    return true;
}


void ConfigGraph::genDot(std::ostream &os, const std::string &name) const {
    os << "graph \"" << name << "\" {\n";
    os << "\tnode [shape=record] ;\n";
    // First, see if we need to deal with MPI ranks
    int maxRank = 0;
    for (ConfigComponentMap_t::const_iterator i = comps.begin() ; i != comps.end() ; ++i) {
        if ( i->rank > maxRank) maxRank = i->rank;
    }

	if ( maxRank > 0 ) {
		for ( int r = 0 ; r <= maxRank ; r++ ) {
			os << "subgraph cluster" << r << " {\n";
			for (ConfigComponentMap_t::const_iterator i = comps.begin() ; i != comps.end() ; ++i) {
				if ( i->rank == r ) {
					os << "\t\t";
					i->genDot(os);
				}
			}
			os << "\t}\n\n";
		}
	} else {
        for (ConfigComponentMap_t::const_iterator i = comps.begin() ; i != comps.end() ; ++i) {
            os << "\t";
            i->genDot(os);
        }
	}


    for (ConfigLinkMap_t::const_iterator i = links.begin() ; i != links.end() ; ++i) {
        os << "\t";
        i->genDot(os);
    }
    os << "\n}\n";
}


bool
ConfigGraph::checkForStructuralErrors()
{
    // Output object for error messages
    Output output = Simulation::getSimulation()->getSimulationOutput();
    
    // Check to make sure there are no dangling links.  A dangling
    // link is found by looking though the links in the graph and
    // making sure there are components on both sides of the link.
    bool found_error = false;
    for( ConfigLinkMap_t::iterator iter = links.begin();
         iter != links.end(); ++iter )
    {
        ConfigLink& clink = *iter;
        // This one should never happen since the slots are
        // initialized in order, but just in case...
        if ( clink.component[0] == ULONG_MAX ) {
            output.output("Found dangling link: %s.  It is connected on one side to component %s.\n",clink.name.c_str(),
                   comps[clink.component[1]].name.c_str());
            found_error = true;
        }
        if ( clink.component[1] == ULONG_MAX ) {
            output.output("Found dangling link: %s.  It is connected on one side to component %s.\n",clink.name.c_str(),
                   comps[clink.component[0]].name.c_str());
            found_error = true;
        }
    }

    // Check to make sure all the component names are unique.  This
    // could be memory intensive for large graphs because we will
    // simply put things in a set and check to see if there are
    // duplicates.
    std::set<std::string> name_set;
    int count = 10;
    for ( ConfigComponentMap_t::iterator iter = comps.begin();
          iter != comps.end(); ++iter )
    {
        ConfigComponent* ccomp = &(*iter);
        if ( name_set.find(ccomp->name) == name_set.end() ) {
            name_set.insert(ccomp->name);
        }
        else {
            found_error = true;
            output.output("Found duplicate component nane: %s\n",ccomp->name.c_str());
            count--;
            if ( count == 0 ) {
                output.output("Maximum name clashes reached, no more checks will be made.\n");
                break;
            }
        }
    }
    
    return found_error;
}


ComponentId_t
ConfigGraph::addComponent(std::string name, std::string type, float weight, int rank)
{
	// comps.push_back(ConfigComponent(comps.size(), name, type, weight, rank, false));
    // return comps.back().id;
	comps.push_back(ConfigComponent(nextCompID, name, type, weight, rank, false));
    return nextCompID++;
}

ComponentId_t
ConfigGraph::addComponent(std::string name, std::string type)
{
	// comps.push_back(ConfigComponent(comps.size(), name, type, 1.0f, 0, false));
    // return comps.back().id;
	comps.push_back(ConfigComponent(nextCompID, name, type, 1.0f, 0, false));
    return nextCompID++;
}

void
ConfigGraph::setComponentRank(ComponentId_t comp_id, int rank)
{
    comps[comp_id].rank = rank;
}

void
ConfigGraph::setComponentWeight(ComponentId_t comp_id, float weight)
{
	comps[comp_id].weight = weight;
}

void
ConfigGraph::addParams(ComponentId_t comp_id, Params& p)
{
    bool bk = comps[comp_id].params.enableVerify(false);
    comps[comp_id].params.insert(p.begin(),p.end());
    comps[comp_id].params.enableVerify(bk);
}

void
ConfigGraph::addParameter(ComponentId_t comp_id, const string key, const string value, bool overwrite)
{
    bool bk = comps[comp_id].params.enableVerify(false);
	if ( overwrite ) {
		comps[comp_id].params[key] = value;
	}
	else {
		comps[comp_id].params.insert(pair<string,string>(key,value));
	}
    comps[comp_id].params.enableVerify(bk);
}

void
ConfigGraph::addLink(ComponentId_t comp_id, string link_name, string port, string latency_str, bool no_cut)
{
	// if ( links.find(link_name) == links.end() ) {
	if ( !links.contains(link_name) ) {
        // links[link_name] = ConfigLink(links.size(), link_name);
        links.insert(ConfigLink(links.size(), link_name));
	}
	ConfigLink &link = links[link_name];
    if ( link.current_ref >= 2 ) {
        cout << "ERROR: Parsing SDL file: Link " << link_name << " referenced more than two times" << endl;
        exit(1);
    }

	// Convert the latency string to a number
	SimTime_t latency = Simulation::getSimulation()->getTimeLord()->getSimCycles(latency_str, "ConfigGraph::addLink");

	int index = link.current_ref++;
	link.component[index] = comp_id;
	link.port[index] = port;
	link.latency[index] = latency;
    link.no_cut = link.no_cut | no_cut;
    
	// comps[comp_id].links.push_back(&link);
	comps[comp_id].links.push_back(link_name);
}

void ConfigGraph::dumpToFile(const std::string filePath, Config* cfg, bool asDot) {
	if ( asDot ) {
		// Attempt to determine graph name
		std::string graphName = filePath;
		size_t off = filePath.rfind('/');
		if ( off != std::string::npos ) {
			graphName = filePath.substr(off+1);
		}

		std::cerr << "Dumping to " << filePath << " with graph name " << graphName << std::endl;

		// Generate file
		std::ofstream os(filePath.c_str());
		genDot(os, graphName);
		os.close();
	} else {
		FILE* dumpFile = fopen(filePath.c_str(), "wt");
		assert(dumpFile);

		ConfigComponentMap_t::iterator comp_itr;
		Params::iterator param_itr;

		fprintf(dumpFile, "# Automatically generated SST Python input\n");
		fprintf(dumpFile, "import sst\n\n");
		fprintf(dumpFile, "# Define SST core options\n");
		fprintf(dumpFile, "sst.setProgramOption(\"timebase\", \"%s\")\n", cfg->timeBase.c_str());
		fprintf(dumpFile, "sst.setProgramOption(\"stopAtCycle\", \"%s\")\n\n", cfg->stopAtCycle.c_str());
		fprintf(dumpFile, "# Define the simulation components\n");
		for(comp_itr = comps.begin(); comp_itr != comps.end(); comp_itr++) {

			fprintf(dumpFile, "%s = sst.Component(\"%s\", \"%s\")\n",
					makeNamePythonSafe(comp_itr->name, "comp_").c_str(),
					escapeString(comp_itr->name).c_str(),
					comp_itr->type.c_str());

			param_itr = comp_itr->params.begin();

			if(param_itr != comp_itr->params.end()) {
				fprintf(dumpFile, "%s.addParams({\n", makeNamePythonSafe(comp_itr->name, "comp_").c_str());
				fprintf(dumpFile, "      \"%s\" : \"\"\"%s\"\"\"", escapeString(Params::getParamName(param_itr->first)).c_str(), escapeString(param_itr->second.c_str()).c_str());
				param_itr++;

				for(; param_itr != comp_itr->params.end(); param_itr++) {
					fprintf(dumpFile, ",\n      \"%s\" : \"\"\"%s\"\"\"",
							escapeString(Params::getParamName(param_itr->first)).c_str(),
							escapeString(param_itr->second).c_str());
				}

				fprintf(dumpFile, "\n})\n");
			}
		}

		fprintf(dumpFile, "\n\n# Define the simulation links\n");

		ConfigLinkMap_t::iterator link_itr;
		for(link_itr = links.begin(); link_itr != links.end(); link_itr++) {
			ConfigComponent* link_left  = &comps[link_itr->component[0]];
			ConfigComponent* link_right = &comps[link_itr->component[1]];

			fprintf(dumpFile, "%s = sst.Link(\"%s\")\n",
					makeNamePythonSafe(link_itr->name, "link_").c_str(), makeNamePythonSafe(link_itr->name, "link_").c_str());
			fprintf(dumpFile, "%s.connect( (%s, \"%s\", \"%" PRIu64 "ps\"), (%s, \"%s\", \"%" PRIu64 "ps\") )\n",
					makeNamePythonSafe(link_itr->name, "link_").c_str(),
					makeNamePythonSafe(link_left->name, "comp_").c_str(),
					escapeString(link_itr->port[0]).c_str(),
					link_itr->latency[0],
					makeNamePythonSafe(link_right->name, "comp_").c_str(),
					escapeString(link_itr->port[1]).c_str(),
					link_itr->latency[1] );
		}

		fprintf(dumpFile, "# End of generated output.\n");
		fclose(dumpFile);
	}
}

std::string ConfigGraph::escapeString(const std::string value) {
	std::string escaped = "";
	uint32_t value_length = (uint32_t) value.size();

//	printf("Attempting to escape [%s]...\n", value.c_str());

	for(uint32_t i = 0; i < value_length; ++i) {
		const char next = value.at(i);

		switch(next) {
		case '\"':
			escaped = escaped + "\\\"";
			break;

		case '\'':
			escaped = escaped + "\\\'";
			break;

		case '\n':
			escaped = escaped + "\\n";
			break;

		default:
			escaped.push_back(next);
			break;
		}
	}

//	printf("Replaced with: [%s].\n", escaped.c_str());

	return escaped;
}

std::string ConfigGraph::makeNamePythonSafe(const std::string name, const std::string namePrefix) {
	const uint32_t name_length = (uint32_t) name.size();
	char* safe_name = (char*) malloc(sizeof(char) * (name_length + 1));
	strcpy(safe_name, name.c_str());

	for(uint32_t i = 0; i < name_length; ++i) {
		switch(safe_name[i]) {
		case '.':
			safe_name[i] = '_';
			break;
		case ':':
			safe_name[i] = '_';
			break;
		case ',':
			safe_name[i] = '_';
			break;
		case '-':
			safe_name[i] = '_';
			break;
		}
	}

	if(name_length > 0 && isdigit(safe_name[0])) {
		std::string safe_name_str = safe_name;
		std::string safe_name_prefix = "s_";

		if(safe_name_str.size() > namePrefix.size()) {
			if(safe_name_str.substr(0, namePrefix.size()) == namePrefix) {
				return safe_name_prefix + safe_name_str;
			} else {
				return namePrefix + safe_name_prefix + safe_name_str;
			}
		} else {
			return namePrefix + safe_name_prefix + safe_name_str;
		}
	} else {
		std::string safe_name_str = safe_name;

		if(safe_name_str.size() > namePrefix.size()) {
			if(safe_name_str.substr(0, namePrefix.size()) == namePrefix) {
				return safe_name_str;
			} else {
				return namePrefix + safe_name_str;
			}
		} else {
			return namePrefix + safe_name_str;
		}
	}
}

ComponentId_t
ConfigGraph::addIntrospector(string name, string type)
{
	comps.push_back(ConfigComponent(nextCompID, name, type, 0.0f, 0, true));
    return nextCompID++;

}


ConfigGraph*
ConfigGraph::getSubGraph(int start_rank, int end_rank)
{
    ConfigGraph* graph = new ConfigGraph();
    
    // Look through all the links.  Add any link that has either side
    // hooked to a component in the specified rank, then put both
    // components in the graph as well.
    for ( ConfigLinkMap_t::iterator it = links.begin(); it != links.end(); ++it ) {
        ConfigLink& link = *it;

        const ConfigComponent& comp0 = comps[link.component[0]];
        const ConfigComponent& comp1 = comps[link.component[1]];

        bool comp0_in_ranks = comp0.rank >= start_rank && comp0.rank <= end_rank;
        bool comp1_in_ranks = comp1.rank >= start_rank && comp1.rank <= end_rank;
        
        // if ( comp0.rank == rank || comp1.rank == rank ) {
        if ( comp0_in_ranks || comp1_in_ranks ) {
            // Clone the link and add to new lin k map
            // graph->links[link.name] = ConfigLink(link);  // Will make a copy into map
            graph->links.insert(ConfigLink(link));  // Will make a copy into map
            ConfigLink& new_link = graph->links[link.name];
            
            // Now add the components to the component map.  If the
            // component is already in the map, add it if not.  Then
            // we just need to add the link to the component.  Remote
            // components will only end up with the links that touch
            // the requested rank.
            if ( !graph->comps.contains(comp0.id) ) {
                if (comp0_in_ranks) graph->comps.insert(comp0.cloneWithoutLinks());
                else graph->comps.insert(comp0.cloneWithoutLinksOrParams());
            }
            if ( !graph->comps.contains(comp1.id) ) {
                if (comp1_in_ranks) graph->comps.insert(comp1.cloneWithoutLinks());
                else graph->comps.insert(comp1.cloneWithoutLinksOrParams());
            }
            graph->comps[comp0.id].links.push_back(new_link.name);
            graph->comps[comp1.id].links.push_back(new_link.name);            
        }
    }
    return graph;
}

ConfigGraph*
ConfigGraph::getSubGraph(std::set<int> rank_set)
{
    ConfigGraph* graph = new ConfigGraph();
    
    // Look through all the links.  Add any link that has either side
    // hooked to a component in the specified rank, then put both
    // components in the graph as well.
    for ( ConfigLinkMap_t::iterator it = links.begin(); it != links.end(); ++it ) {
        const ConfigLink& link = *it;

        const ConfigComponent& comp0 = comps[link.component[0]];
        const ConfigComponent& comp1 = comps[link.component[1]];

        bool comp0_in_ranks = (rank_set.find(comp0.rank) != rank_set.end());
        bool comp1_in_ranks = (rank_set.find(comp1.rank) != rank_set.end());
        
        // if ( comp0.rank == rank || comp1.rank == rank ) {
        if ( comp0_in_ranks || comp1_in_ranks ) {
            // Clone the link and add to new lin k map
            graph->links.insert(ConfigLink(link));  // Will make a copy into map

            // Now add the components to the component map.  If the
            // component is already in the map, add it if not.  Then
            // we just need to add the link to the component.  Remote
            // components will only end up with the links that touch
            // the requested rank.
            if ( !graph->comps.contains(comp0.id) ) {
                if (comp0_in_ranks) graph->comps.insert(comp0.cloneWithoutLinks());
                else graph->comps.insert(comp0.cloneWithoutLinksOrParams());
            }
            if ( !graph->comps.contains(comp1.id) ) {
                if (comp1_in_ranks) graph->comps.insert(comp1.cloneWithoutLinks());
                else graph->comps.insert(comp1.cloneWithoutLinksOrParams());
            }
            graph->comps[comp0.id].links.push_back(link.name);
            graph->comps[comp1.id].links.push_back(link.name);            
        }
    }
    return graph;    
}

} // namespace SST
