/*==============================================================================

Program: 3D Slicer

Copyright (c) Kitware Inc.

See COPYRIGHT.txt
or http://www.slicer.org/copyright/copyright.txt for details.

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.

This file was originally developed by Julien Finet, Kitware Inc.
and was partially funded by Allen Institute

==============================================================================*/

// MarkupsModule/MRML includes
#include <vtkMRMLMarkupsSplinesNode.h>
#include <vtkMRMLMarkupsDisplayNode.h>

// MarkupsModule/MRMLDisplayableManager includes
#include "vtkMRMLMarkupsSplinesDisplayableManager3D.h"

// MRML includes
#include <vtkMRMLApplicationLogic.h>
#include <vtkMRMLDisplayableManagerGroup.h>
#include <vtkMRMLInteractionNode.h>
#include <vtkMRMLMarkupsClickCounter.h>
#include <vtkMRMLModelDisplayableManager.h>
#include <vtkMRMLScene.h>
#include <vtkMRMLSelectionNode.h>
#include <vtkMRMLViewNode.h>

// VTK includes
#include <vtkAbstractWidget.h>
#include <vtkCallbackCommand.h>
#include <vtkCoordinate.h>
#include <vtkEventBroker.h>
#include <vtkInteractorStyle.h>
#include <vtkMatrix4x4.h>
#include <vtkNew.h>
#include <vtkObjectFactory.h>
#include <vtkPlaneSource.h>
#include <vtkPointPlacer.h>
#include <vtkPolyDataMapper.h>
#include <vtkRenderer.h>
#include <vtkRenderWindowInteractor.h>
#include <vtkSmartPointer.h>
#include <vtkSplineRepresentation.h>
#include <vtkSplineWidget2.h>

// STD includes
#include <sstream>
#include <string>

//---------------------------------------------------------------------------
vtkStandardNewMacro(vtkMRMLMarkupsSplinesDisplayableManager3D);

//---------------------------------------------------------------------------
class vtkMRMLMarkupsSplinesDisplayableManager3D::vtkInternal
{
public:
  vtkInternal(vtkMRMLMarkupsSplinesDisplayableManager3D* external);
  ~vtkInternal();

  typedef std::vector<vtkSplineWidget2*> WidgetListType;
  struct Pipeline
  {
    WidgetListType Widgets;
  };

  typedef std::map<vtkMRMLMarkupsDisplayNode*, Pipeline* > PipelinesCacheType;
  PipelinesCacheType DisplayPipelines;

  typedef std::map<vtkMRMLMarkupsSplinesNode*, std::set<vtkMRMLMarkupsDisplayNode*> >
    MarkupToDisplayCacheType;
  MarkupToDisplayCacheType MarkupToDisplayNodes;

  typedef std::map <vtkSplineWidget2*, vtkMRMLMarkupsDisplayNode* > WidgetToPipelineMapType;
  WidgetToPipelineMapType WidgetMap;

  // Node
  void AddNode(vtkMRMLMarkupsSplinesNode* displayableNode);
  void RemoveNode(vtkMRMLMarkupsSplinesNode* displayableNode);
  void UpdateDisplayableNode(vtkMRMLMarkupsSplinesNode *node);

  // Display Nodes
  void AddDisplayNode(vtkMRMLMarkupsSplinesNode*, vtkMRMLMarkupsDisplayNode*);
  void UpdateDisplayNode(vtkMRMLMarkupsDisplayNode* displayNode);
  void UpdateDisplayNodePipeline(vtkMRMLMarkupsDisplayNode*, Pipeline*);
  void RemoveDisplayNode(vtkMRMLMarkupsDisplayNode* displayNode);
  void SetTransformDisplayProperty(vtkMRMLMarkupsDisplayNode *displayNode, vtkActor* actor);

  // Widget
  void UpdateWidgetFromNode(vtkMRMLMarkupsDisplayNode*, vtkMRMLMarkupsSplinesNode*, Pipeline*);
  void UpdateNodeFromWidget(vtkSplineWidget2*);
  void UpdateActiveNode();

  // Observations
  void AddObservations(vtkMRMLMarkupsSplinesNode* node);
  void RemoveObservations(vtkMRMLMarkupsSplinesNode* node);

  void AddObservationsToInteractionNode(vtkMRMLInteractionNode*);
  void RemoveObservationsFromInteractionNode(vtkMRMLInteractionNode*);

  // Helper functions
  bool UseDisplayNode(vtkMRMLMarkupsDisplayNode* displayNode);
  void ClearDisplayableNodes();
  void ClearPipeline(Pipeline* pipeline);
  vtkSplineWidget2* CreateSplineWidget() const;
  std::vector<int> EventsToObserve() const;
  void StopInteraction();

public:
  vtkMRMLMarkupsSplinesDisplayableManager3D* External;
  bool AddingNode;

  vtkSmartPointer<vtkPointPlacer> PointPlacer;
};

//---------------------------------------------------------------------------
// vtkInternal methods

//---------------------------------------------------------------------------
vtkMRMLMarkupsSplinesDisplayableManager3D::vtkInternal
::vtkInternal(vtkMRMLMarkupsSplinesDisplayableManager3D * external)
  : External(external),
  AddingNode(false)
{
  this->PointPlacer = vtkSmartPointer<vtkPointPlacer>::New();
}

//---------------------------------------------------------------------------
vtkMRMLMarkupsSplinesDisplayableManager3D::vtkInternal::~vtkInternal()
{
  this->ClearDisplayableNodes();
}

//---------------------------------------------------------------------------
bool vtkMRMLMarkupsSplinesDisplayableManager3D::vtkInternal
::UseDisplayNode(vtkMRMLMarkupsDisplayNode* displayNode)
{
  // allow annotations to appear only in designated viewers
  return displayNode && displayNode->IsDisplayableInView(
    this->External->GetMRMLViewNode()->GetID());
}

//---------------------------------------------------------------------------
void vtkMRMLMarkupsSplinesDisplayableManager3D::vtkInternal
::AddNode(vtkMRMLMarkupsSplinesNode* node)
{
  if (this->AddingNode || !node)
  {
    return;
  }

  this->AddingNode = true;

  // Add Display Nodes
  this->AddObservations(node);
  for (int i = 0; i< node->GetNumberOfDisplayNodes(); i++)
  {
    vtkMRMLMarkupsDisplayNode* displayNode =
      vtkMRMLMarkupsDisplayNode::SafeDownCast(node->GetNthDisplayNode(i));
    if (this->UseDisplayNode(displayNode))
    {
      this->MarkupToDisplayNodes[node].insert(displayNode);
      this->AddDisplayNode(node, displayNode);
    }
  }

  this->AddingNode = false;
}

//---------------------------------------------------------------------------
void vtkMRMLMarkupsSplinesDisplayableManager3D::vtkInternal
::RemoveNode(vtkMRMLMarkupsSplinesNode* node)
{
  if (!node)
  {
    return;
  }

  vtkInternal::MarkupToDisplayCacheType::iterator displayableIt =
    this->MarkupToDisplayNodes.find(node);
  if (displayableIt == this->MarkupToDisplayNodes.end())
  {
    return;
  }

  std::set<vtkMRMLMarkupsDisplayNode *> displayNodes = displayableIt->second;
  std::set<vtkMRMLMarkupsDisplayNode*>::iterator diter;
  for (diter = displayNodes.begin(); diter != displayNodes.end(); ++diter)
  {
    this->RemoveDisplayNode(*diter);
  }
  this->RemoveObservations(node);
  this->MarkupToDisplayNodes.erase(displayableIt);
}

//---------------------------------------------------------------------------
void vtkMRMLMarkupsSplinesDisplayableManager3D::vtkInternal
::UpdateDisplayableNode(vtkMRMLMarkupsSplinesNode* mNode)
{
  // Update the pipeline for all tracked DisplayableNode
  PipelinesCacheType::iterator pipelinesIter;
  std::set<vtkMRMLMarkupsDisplayNode*> displayNodes = this->MarkupToDisplayNodes[mNode];
  std::set<vtkMRMLMarkupsDisplayNode*>::iterator diter;
  for (diter = displayNodes.begin(); diter != displayNodes.end(); ++diter)
  {
    pipelinesIter = this->DisplayPipelines.find(*diter);
    if (pipelinesIter != this->DisplayPipelines.end())
    {
      Pipeline* pipeline = pipelinesIter->second;
      this->UpdateDisplayNodePipeline(pipelinesIter->first, pipeline);
    }
  }
}

//---------------------------------------------------------------------------
void vtkMRMLMarkupsSplinesDisplayableManager3D::vtkInternal
::ClearPipeline(Pipeline* pipeline)
{
  for (size_t i = 0; i < pipeline->Widgets.size(); ++i)
  {
    vtkSplineWidget2* widget = pipeline->Widgets[i];
    this->WidgetMap.erase(widget);
    widget->Delete();
  }
  pipeline->Widgets.clear();
}

//---------------------------------------------------------------------------
void vtkMRMLMarkupsSplinesDisplayableManager3D::vtkInternal
::RemoveDisplayNode(vtkMRMLMarkupsDisplayNode* displayNode)
{
  PipelinesCacheType::iterator actorsIt = this->DisplayPipelines.find(displayNode);
  if (actorsIt == this->DisplayPipelines.end())
  {
    return;
  }
  Pipeline* pipeline = actorsIt->second;
  this->ClearPipeline(pipeline);
  delete pipeline;
  this->DisplayPipelines.erase(actorsIt);
}

//---------------------------------------------------------------------------
vtkSplineWidget2*
vtkMRMLMarkupsSplinesDisplayableManager3D::vtkInternal::CreateSplineWidget() const
{
  vtkSplineWidget2* widget = vtkSplineWidget2::New();
  widget->CreateDefaultRepresentation();
  vtkSplineRepresentation* rep =
    vtkSplineRepresentation::SafeDownCast(widget->GetRepresentation());
  widget->AddObserver(
    vtkCommand::InteractionEvent, this->External->GetWidgetsCallbackCommand());

  rep->ProjectToPlaneOff();

  widget->SetInteractor(this->External->GetInteractor());
  widget->SetCurrentRenderer(this->External->GetRenderer());

  return widget;
}

//---------------------------------------------------------------------------
void vtkMRMLMarkupsSplinesDisplayableManager3D::vtkInternal
::AddDisplayNode(vtkMRMLMarkupsSplinesNode* mNode, vtkMRMLMarkupsDisplayNode* displayNode)
{
  if (!mNode || !displayNode)
  {
    return;
  }

  // Do not add the display node if it is already associated with a pipeline object.
  // This happens when a node already associated with a display node
  // is copied into an other (using vtkMRMLNode::Copy()) and is added to the scene afterward.
  // Related issue are #3428 and #2608
  PipelinesCacheType::iterator it;
  it = this->DisplayPipelines.find(displayNode);
  if (it != this->DisplayPipelines.end())
  {
    return;
  }

  // Create pipeline
  Pipeline* pipeline = new Pipeline();
  this->DisplayPipelines.insert(std::make_pair(displayNode, pipeline));

  // Update cached matrices. Calls UpdateDisplayNodePipeline
  this->UpdateDisplayableNode(mNode);
}

//---------------------------------------------------------------------------
void vtkMRMLMarkupsSplinesDisplayableManager3D::vtkInternal
::UpdateDisplayNode(vtkMRMLMarkupsDisplayNode* displayNode)
{
  // If the DisplayNode already exists, just update.
  //   otherwise, add as new node
  if (!displayNode)
  {
    return;
  }
  PipelinesCacheType::iterator it;
  it = this->DisplayPipelines.find(displayNode);
  if (it != this->DisplayPipelines.end())
  {
    this->UpdateDisplayNodePipeline(displayNode, it->second);
  }
  else
  {
    this->AddNode(
      vtkMRMLMarkupsSplinesNode::SafeDownCast(displayNode->GetDisplayableNode()));
  }
}

//---------------------------------------------------------------------------
void vtkMRMLMarkupsSplinesDisplayableManager3D::vtkInternal
::UpdateActiveNode()
{
  vtkMRMLSelectionNode* selectionNode = this->External->GetSelectionNode();
  vtkMRMLMarkupsSplinesNode* splinesNode =
    vtkMRMLMarkupsSplinesNode::SafeDownCast(
      this->External->GetMRMLScene()->GetNodeByID(
        selectionNode ? selectionNode->GetActivePlaceNodeID() : NULL));

  if (!splinesNode)
  {
    splinesNode = vtkMRMLMarkupsSplinesNode::SafeDownCast(
      this->External->GetMRMLScene()->AddNode(vtkMRMLMarkupsSplinesNode::New()));
    splinesNode->Delete();
  }

  vtkRenderWindowInteractor* interactor = this->External->GetInteractor();
  double x = interactor->GetEventPosition()[0];
  double y = interactor->GetEventPosition()[1];

  // ModelDisplayableManager is expected to be instantiated !
  vtkMRMLModelDisplayableManager * modelDisplayableManager =
    vtkMRMLModelDisplayableManager::SafeDownCast(
      this->External->GetMRMLDisplayableManagerGroup()
        ->GetDisplayableManagerByClassName("vtkMRMLModelDisplayableManager"));
  assert(modelDisplayableManager);

  double windowHeight = interactor->GetRenderWindow()->GetSize()[1];

  double yNew = windowHeight - y - 1;

  double world[4];
  if (modelDisplayableManager->Pick(x, yNew))
  {
    double* pickedWorldCoordinates = modelDisplayableManager->GetPickedRAS();
    world[0] = pickedWorldCoordinates[0];
    world[1] = pickedWorldCoordinates[1];
    world[2] = pickedWorldCoordinates[2];
    world[3] = 1;
  }
  else
  {
    // we could not pick so just convert to world coordinates
    vtkInteractorObserver::ComputeDisplayToWorld(
      this->External->GetRenderer(), x, y, 0, world);
  }

  int currentSpline = splinesNode->GetCurrentSpline();
  int wasModifying = splinesNode->StartModify();

  if (currentSpline == -1)
  {
    splinesNode->AddSpline(vtkVector3d(world[0], world[1], world[2]));
  }
  else
  {
    splinesNode->AddPointToNthMarkup(
      vtkVector3d(world[0], world[1], world[2]), currentSpline);
  }
  splinesNode->EndModify(wasModifying);

  this->External->RequestRender();
}

//---------------------------------------------------------------------------
void vtkMRMLMarkupsSplinesDisplayableManager3D::vtkInternal::StopInteraction()
{
  vtkMRMLInteractionNode* interactionNode =
    this->External->GetInteractionNode();
  interactionNode->SwitchToViewTransformMode();

  // Lock the active node.
  vtkMRMLSelectionNode* selectionNode = this->External->GetSelectionNode();
  vtkMRMLMarkupsSplinesNode* splinesNode =
    vtkMRMLMarkupsSplinesNode::SafeDownCast(
      this->External->GetMRMLScene()->GetNodeByID(
        selectionNode ? selectionNode->GetActivePlaceNodeID() : NULL));
  if (splinesNode)
  {
    int currentSpline = splinesNode->GetCurrentSpline();
    if (currentSpline != -1)
    {
      splinesNode->SetNthMarkupLocked(currentSpline, true);
    }

    splinesNode->SetCurrentSpline(-1);
  }

  this->External->RequestRender();
}

//---------------------------------------------------------------------------
void vtkMRMLMarkupsSplinesDisplayableManager3D::vtkInternal
::UpdateWidgetFromNode(vtkMRMLMarkupsDisplayNode* displayNode,
  vtkMRMLMarkupsSplinesNode* splinesNode,
  Pipeline* pipeline)
{
  size_t numberOfWidgets = pipeline->Widgets.size();

  // If we need to remove plane widgets, we don't know which one
  // were deleted, so remove all of them.
  if (splinesNode && splinesNode->GetNumberOfMarkups() < numberOfWidgets)
  {
    this->ClearPipeline(pipeline);
  }
  // Add any missing plane widget.
  if (splinesNode && splinesNode->GetNumberOfMarkups() > numberOfWidgets)
  {
    for (int i = numberOfWidgets; i < splinesNode->GetNumberOfMarkups(); ++i)
    {
      vtkSplineWidget2* widget = this->CreateSplineWidget();
      pipeline->Widgets.push_back(widget);
      this->WidgetMap[widget] = displayNode;
    }
  }

  for (size_t n = 0; n < pipeline->Widgets.size(); ++n)
  {
    vtkSplineWidget2* widget = pipeline->Widgets[n];
    bool visible = displayNode->GetVisibility();
    if (visible)
    {
      visible = splinesNode ? splinesNode->GetNthMarkupVisibility(n) : false;
    }
    widget->SetEnabled(visible);

    int processEvents = splinesNode->GetLocked() ? 0 :
      (splinesNode->GetNthMarkupLocked(n) ? 0 : 1);
    widget->SetProcessEvents(processEvents);

    if (visible)
    {
      vtkSplineRepresentation* rep =
        vtkSplineRepresentation::SafeDownCast(widget->GetRepresentation());
      int numberOfPoints = splinesNode->GetNumberOfPointsInNthMarkup(n);
      rep->SetNumberOfHandles(numberOfPoints);

      for (int i = 0; i < numberOfPoints; ++i)
      {
        double world[4];
        splinesNode->GetMarkupPointWorld(n, i, world);
        rep->SetHandlePosition(i, world);
      }
      rep->SetClosed(splinesNode->GetNthSplineClosed(n) ? 1 : 0);

      rep->BuildRepresentation();
    }
  }
}

//---------------------------------------------------------------------------
void vtkMRMLMarkupsSplinesDisplayableManager3D::vtkInternal
::UpdateNodeFromWidget(vtkSplineWidget2* widget)
{
  assert(widget);
  vtkMRMLMarkupsDisplayNode* displayNode = this->WidgetMap.at(widget);
  assert(displayNode);
  vtkMRMLMarkupsSplinesNode* splinesNode =
    vtkMRMLMarkupsSplinesNode::SafeDownCast(displayNode->GetDisplayableNode());

  vtkSplineRepresentation* rep =
    vtkSplineRepresentation::SafeDownCast(widget->GetRepresentation());

  Pipeline* pipeline = this->DisplayPipelines[displayNode];
  size_t index =
    std::find(pipeline->Widgets.begin(), pipeline->Widgets.end(), widget)
    - pipeline->Widgets.begin();

  int wasModifying = splinesNode->StartModify();

  if (!splinesNode->MarkupExists(index))
  {
    while (!splinesNode->MarkupExists(index))
    {
      splinesNode->AddSpline(vtkVector3d(0.0, 0.0, 0.0));
    }
  }

  int numberOfPoints = rep->GetNumberOfHandles();
  for (int i = 0; i < numberOfPoints; ++i)
  {
    vtkVector3d pos;
    rep->GetHandlePosition(i, pos.GetData());

    if (i >= splinesNode->GetNumberOfPointsInNthMarkup(index))
    {
      splinesNode->AddPointToNthMarkup(pos, index);
    }
    else
    {
      splinesNode->SetMarkupPoint(index, i, pos[0], pos[1], pos[2]);
    }
  }

  splinesNode->EndModify(wasModifying);
}

//---------------------------------------------------------------------------
void vtkMRMLMarkupsSplinesDisplayableManager3D::vtkInternal
::UpdateDisplayNodePipeline(
  vtkMRMLMarkupsDisplayNode* displayNode, Pipeline* pipeline)
{
  if (!displayNode || !pipeline)
  {
    return;
  }

  vtkMRMLMarkupsSplinesNode* splinesNode =
    vtkMRMLMarkupsSplinesNode::SafeDownCast(displayNode->GetDisplayableNode());

  this->UpdateWidgetFromNode(displayNode, splinesNode, pipeline);
}

//---------------------------------------------------------------------------
std::vector<int> vtkMRMLMarkupsSplinesDisplayableManager3D::vtkInternal
::EventsToObserve() const
{
  std::vector<int> events;
  events.push_back(vtkMRMLDisplayableNode::DisplayModifiedEvent);
  events.push_back(vtkMRMLMarkupsSplinesNode::MarkupAddedEvent);
  events.push_back(vtkMRMLMarkupsSplinesNode::MarkupRemovedEvent);
  events.push_back(vtkMRMLMarkupsSplinesNode::NthMarkupModifiedEvent);
  events.push_back(vtkMRMLMarkupsSplinesNode::PointModifiedEvent);
  events.push_back(vtkCommand::ModifiedEvent);
  return events;
}

//---------------------------------------------------------------------------
void vtkMRMLMarkupsSplinesDisplayableManager3D::vtkInternal
::AddObservations(vtkMRMLMarkupsSplinesNode* node)
{
  std::vector<int> events = this->EventsToObserve();
  vtkEventBroker* broker = vtkEventBroker::GetInstance();
  for (size_t i = 0; i < events.size(); ++i)
  {
    if (!broker->GetObservationExist(node, events[i], this->External, this->External->GetMRMLNodesCallbackCommand()))
    {
      broker->AddObservation(node, events[i], this->External, this->External->GetMRMLNodesCallbackCommand());
    }
  }
}

//---------------------------------------------------------------------------
void vtkMRMLMarkupsSplinesDisplayableManager3D::vtkInternal
::RemoveObservations(vtkMRMLMarkupsSplinesNode* node)
{
  vtkEventBroker* broker = vtkEventBroker::GetInstance();
  vtkEventBroker::ObservationVector observations;
  std::vector<int> events = this->EventsToObserve();
  for (size_t i = 0; i < events.size(); ++i)
  {
    observations = broker->GetObservations(node, events[i], this->External, this->External->GetMRMLNodesCallbackCommand());
  }
  broker->RemoveObservations(observations);
}

//---------------------------------------------------------------------------
void vtkMRMLMarkupsSplinesDisplayableManager3D::vtkInternal
::AddObservationsToInteractionNode(vtkMRMLInteractionNode* interactionNode)
{
  if (!interactionNode)
  {
    return;
  }

  vtkNew<vtkIntArray> interactionEvents;
  interactionEvents->InsertNextValue(vtkMRMLInteractionNode::InteractionModeChangedEvent);
  interactionEvents->InsertNextValue(vtkMRMLInteractionNode::EndPlacementEvent);
  this->External->GetMRMLNodesObserverManager()->AddObjectEvents(
    interactionNode, interactionEvents.GetPointer());
}

//---------------------------------------------------------------------------
void vtkMRMLMarkupsSplinesDisplayableManager3D::vtkInternal
::RemoveObservationsFromInteractionNode(vtkMRMLInteractionNode* interactionNode)
{
  this->External->GetMRMLNodesObserverManager()->RemoveObjectEvents(interactionNode);
}

//---------------------------------------------------------------------------
void vtkMRMLMarkupsSplinesDisplayableManager3D::vtkInternal::ClearDisplayableNodes()
{
  while (this->MarkupToDisplayNodes.size() > 0)
  {
    this->RemoveNode(this->MarkupToDisplayNodes.begin()->first);
  }
}

//---------------------------------------------------------------------------
// vtkMRMLMarkupsSplinesDisplayableManager3D methods
//---------------------------------------------------------------------------
vtkMRMLMarkupsSplinesDisplayableManager3D::vtkMRMLMarkupsSplinesDisplayableManager3D()
{
  this->Internal = new vtkInternal(this);
}

//---------------------------------------------------------------------------
vtkMRMLMarkupsSplinesDisplayableManager3D::~vtkMRMLMarkupsSplinesDisplayableManager3D()
{
  delete this->Internal;
  this->Internal = NULL;
}

//---------------------------------------------------------------------------
void vtkMRMLMarkupsSplinesDisplayableManager3D::PrintSelf(ostream& os, vtkIndent indent)
{
  this->Superclass::PrintSelf(os, indent);
  os << indent << "vtkMRMLMarkupsSplinesDisplayableManager3D: "
    << this->GetClassName() << "\n";
}

//---------------------------------------------------------------------------
void vtkMRMLMarkupsSplinesDisplayableManager3D
::SetMRMLSceneInternal(vtkMRMLScene* newScene)
{
  Superclass::SetMRMLSceneInternal(newScene);

  if (newScene)
  {
    this->Internal->AddObservationsToInteractionNode(
      this->GetInteractionNode());
  }
  else
  {
    this->Internal->RemoveObservationsFromInteractionNode(
      this->GetInteractionNode());
  }
}

//---------------------------------------------------------------------------
void vtkMRMLMarkupsSplinesDisplayableManager3D
::OnMRMLSceneNodeAdded(vtkMRMLNode* node)
{
  if (!node)
  {
    return;
  }

  if (node->IsA("vtkMRMLInteractionNode"))
  {
    this->Internal->AddObservationsToInteractionNode(
      vtkMRMLInteractionNode::SafeDownCast(node));
    return;
  }

  if (!node->IsA("vtkMRMLMarkupsSplinesNode"))
  {
    return;
  }

  // Escape if the scene a scene is being closed, imported or connected
  if (this->GetMRMLScene()->IsBatchProcessing())
  {
    this->SetUpdateFromMRMLRequested(1);
    return;
  }

  this->Internal->AddNode(vtkMRMLMarkupsSplinesNode::SafeDownCast(node));
  this->RequestRender();
}

//---------------------------------------------------------------------------
void vtkMRMLMarkupsSplinesDisplayableManager3D::OnMRMLSceneNodeRemoved(vtkMRMLNode* node)
{
  if (node
    && (!node->IsA("vtkMRMLMarkupsSplinesNode"))
    && (!node->IsA("vtkMRMLMarkupDisplayNode")))
  {
    return;
  }

  vtkMRMLMarkupsSplinesNode* splinesNode = vtkMRMLMarkupsSplinesNode::SafeDownCast(node);
  vtkMRMLMarkupsDisplayNode* displayNode = vtkMRMLMarkupsDisplayNode::SafeDownCast(node);

  bool modified = false;
  if (splinesNode)
  {
    this->Internal->RemoveNode(splinesNode);
    modified = true;
  }
  else if (displayNode)
  {
    this->Internal->RemoveDisplayNode(displayNode);
    modified = true;
  }
  if (modified)
  {
    this->RequestRender();
  }
}

//---------------------------------------------------------------------------
void vtkMRMLMarkupsSplinesDisplayableManager3D
::ProcessMRMLNodesEvents(vtkObject* caller, unsigned long event, void* callData)
{
  vtkMRMLScene* scene = this->GetMRMLScene();
  if (scene->IsBatchProcessing())
  {
    return;
  }

  vtkMRMLMarkupsSplinesNode* splinesNode = vtkMRMLMarkupsSplinesNode::SafeDownCast(caller);
  vtkMRMLMarkupsDisplayNode* displayNode = vtkMRMLMarkupsDisplayNode::SafeDownCast(caller);
  vtkMRMLInteractionNode* interactionNode = vtkMRMLInteractionNode::SafeDownCast(caller);

  if (splinesNode)
  {
    displayNode = reinterpret_cast<vtkMRMLMarkupsDisplayNode*>(callData);
    if (displayNode && (event == vtkMRMLDisplayableNode::DisplayModifiedEvent))
    {
      this->Internal->UpdateDisplayNode(displayNode);
      this->RequestRender();
    }
    else
    {
      this->Internal->UpdateDisplayableNode(splinesNode);
      this->RequestRender();
    }
  }
  else if (interactionNode)
  {
    if (event == vtkMRMLInteractionNode::EndPlacementEvent)
    {
      this->Internal->StopInteraction();
    }
  }
  else
  {
    this->Superclass::ProcessMRMLNodesEvents(caller, event, callData);
  }
}

//---------------------------------------------------------------------------
void vtkMRMLMarkupsSplinesDisplayableManager3D::UpdateFromMRML()
{
  this->SetUpdateFromMRMLRequested(0);

  vtkMRMLScene* scene = this->GetMRMLScene();
  if (!scene)
  {
    return;
  }
  this->Internal->ClearDisplayableNodes();

  vtkMRMLMarkupsSplinesNode* mNode = NULL;
  std::vector<vtkMRMLNode *> mNodes;
  int nnodes = scene ? scene->GetNodesByClass("vtkMRMLMarkupsSplinesNode", mNodes) : 0;
  for (int i = 0; i < nnodes; i++)
  {
    mNode = vtkMRMLMarkupsSplinesNode::SafeDownCast(mNodes[i]);
    if (mNode)
    {
      this->Internal->AddNode(mNode);
    }
  }
  this->RequestRender();
}

//---------------------------------------------------------------------------
void vtkMRMLMarkupsSplinesDisplayableManager3D
::OnInteractorStyleEvent(int eventid)
{
  vtkMRMLSelectionNode* selectionNode =
    this->GetMRMLApplicationLogic()->GetSelectionNode();
  if (!selectionNode || !selectionNode->GetActivePlaceNodeClassName())
  {
    return;
  }

  if (strcmp(selectionNode->GetActivePlaceNodeClassName(), "vtkMRMLMarkupsSplinesNode") != 0)
  {
    return;
  }

  if (eventid == vtkCommand::LeftButtonReleaseEvent)
  {
    vtkMRMLInteractionNode* interactionNode = this->GetInteractionNode();
    if (interactionNode->GetCurrentInteractionMode() == vtkMRMLInteractionNode::Place)
    {
      this->Internal->UpdateActiveNode();

      if (interactionNode->GetPlaceModePersistence() != 1)
      {
        interactionNode->SetCurrentInteractionMode(vtkMRMLInteractionNode::ViewTransform);
      }

    }
  }
  else if (eventid == vtkCommand::RightButtonReleaseEvent)
  {
    this->Internal->StopInteraction();
  }
}


//---------------------------------------------------------------------------
void vtkMRMLMarkupsSplinesDisplayableManager3D::UnobserveMRMLScene()
{
  this->Internal->ClearDisplayableNodes();
}

//---------------------------------------------------------------------------
void vtkMRMLMarkupsSplinesDisplayableManager3D::OnMRMLSceneStartClose()
{
  this->Internal->ClearDisplayableNodes();
}

//---------------------------------------------------------------------------
void vtkMRMLMarkupsSplinesDisplayableManager3D::OnMRMLSceneEndClose()
{
  this->SetUpdateFromMRMLRequested(1);
}

//---------------------------------------------------------------------------
void vtkMRMLMarkupsSplinesDisplayableManager3D::OnMRMLSceneEndBatchProcess()
{
  this->SetUpdateFromMRMLRequested(1);
  this->RequestRender();
}

//---------------------------------------------------------------------------
void vtkMRMLMarkupsSplinesDisplayableManager3D::Create()
{
  Superclass::Create();
  this->SetUpdateFromMRMLRequested(1);
}

//---------------------------------------------------------------------------
void vtkMRMLMarkupsSplinesDisplayableManager3D
::ProcessWidgetsEvents(vtkObject* caller, unsigned long event, void* callData)
{
  Superclass::ProcessWidgetsEvents(caller, event, callData);

  vtkSplineWidget2* widget = vtkSplineWidget2::SafeDownCast(caller);
  if (widget)
  {
    this->Internal->UpdateNodeFromWidget(widget);
    this->RequestRender();
  }
}
