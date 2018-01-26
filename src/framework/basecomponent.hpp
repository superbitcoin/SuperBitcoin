/*************************************************
 * File name:		// basecomponent.hpp
 * Author:
 * Date: 		    //2018.01.26
 * Description:		// The base class of a component, and all components are derived from this class

 * Others:		    //
 * History:		    // 2018.01.26

 * 1. Date:
 * Author:
 * Modification:
*************************************************/
#pragma once
#include <boost/program_options.hpp>
#include <boost/preprocessor/seq/for_each.hpp>
#include <string>
#include <vector>
#include <map>

namespace appbase {

   using boost::program_options::options_description;
   using boost::program_options::variables_map;
   using std::string;
   using std::vector;
   using std::map;

   class CBaseComponent {
      public:
         enum state {
            registered, ///< the plugin is constructed but doesn't do anything
            initialized, ///< the plugin has initialized any state required but is idle
            started, ///< the plugin is actively running
            stopped ///< the plugin is no longer running
         };

         virtual ~CBaseComponent(){}
         virtual state GetState()const = 0;
         virtual const std::string& Name()const  = 0;
         virtual void SetProgramOptions( options_description& cli, options_description& cfg ) = 0;
         virtual void Initialize(const variables_map& options) = 0;
         virtual void Startup() = 0;
         virtual void Shutdown() = 0;
   };

   template<typename Impl>
   class CComponent;
}
