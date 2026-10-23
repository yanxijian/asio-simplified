#include "sasio/execution_context.hpp"

namespace sasio {

execution_context::~execution_context()
{
  shutdown_services();
}

void execution_context::shutdown_services()
{
  // Reverse order of creation: a service that depends on another is torn down
  // before the service it depends on.
  for (auto it = order_.rbegin(); it != order_.rend(); ++it)
    (*it)->shutdown();

  services_.clear();
  order_.clear();
}

} // namespace sasio
