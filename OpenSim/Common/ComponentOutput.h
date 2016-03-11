#ifndef OPENSIM_COMPONENT_OUTPUT_H_
#define OPENSIM_COMPONENT_OUTPUT_H_
/* -------------------------------------------------------------------------- *
 *                      OpenSim:  ComponentOutput.h                           *
 * -------------------------------------------------------------------------- *
 * The OpenSim API is a toolkit for musculoskeletal modeling and simulation.  *
 * See http://opensim.stanford.edu and the NOTICE file for more information.  *
 * OpenSim is developed at Stanford University and supported by the US        *
 * National Institutes of Health (U54 GM072970, R24 HD065690) and by DARPA    *
 * through the Warrior Web program.                                           *
 *                                                                            *
 * Copyright (c) 2005-2014 Stanford University and the Authors                *
 * Author(s): Ajay Seth                                                       *
 *                                                                            *
 * Licensed under the Apache License, Version 2.0 (the "License"); you may    *
 * not use this file except in compliance with the License. You may obtain a  *
 * copy of the License at http://www.apache.org/licenses/LICENSE-2.0.         *
 *                                                                            *
 * Unless required by applicable law or agreed to in writing, software        *
 * distributed under the License is distributed on an "AS IS" BASIS,          *
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.   *
 * See the License for the specific language governing permissions and        *
 * limitations under the License.                                             *
 * -------------------------------------------------------------------------- */

/** @file
 * This file defines the Output class, which formalizes an output (signal)  
 * that a Component produces. This can be the tension in a force element, the 
 * location of a body, metabolic energy consumption of a model, etc...
 * It is the obligation of the Component to define its outputs.
 */

// INCLUDES
#include <functional>
#include <unordered_map>

namespace OpenSim {

class Component;

//=============================================================================
//                           OPENSIM COMPONENT OUTPUT
//=============================================================================
/**
 * Output formalizes the access to a value of interest computed by the 
 * owning Component. The value is then exposed and easily accessible for use by
 * other components (e.g. to satisfy an Input).
 * The purpose of an Output is to bind a value of interest to a component's 
 * member function (generator), and provide a generic interface to the value, 
 * its type and label so it can be easily identified. It also specifies the 
 * realization (computational) stage at which the value is valid, so that a 
 * caller can provide adequate error handling.
 *
 * For example, a Body can have its position transformation with respect to  
 * ground as an Output, which is only accessible when the model has been 
 * realized to the Position stage or beyond, in which case it depends on the
 * Position stage. The validity of data flow can be checked prior to
 * initiating a simulation.
 *
 * An Output is intended to lightweight and adds no computational overhead
 * if the output goes unused. When an Output's value is called upon,
 * the overhead is a single redirect to the corresponding member function
 * for the value.
 *
 * @author  Ajay Seth
 */

class AbstractChannel {
public:
    virtual ~AbstractChannel() = default;
    virtual const std::string& getName() const = 0;
};

class OSIMCOMMON_API AbstractOutput {
public:
    AbstractOutput() : dependsOnStage(SimTK::Stage::Infinity) {}
    AbstractOutput(const std::string& name, SimTK::Stage dependsOnStage,
                   bool isList) :
        name(name), dependsOnStage(dependsOnStage), _isList(isList) {}
    virtual ~AbstractOutput() { }

    /** Output's name */
    const std::string& getName() const { return name; }
    /** Output's dependence on System being realized to at least this System::Stage */
    const SimTK::Stage& getDependsOnStage() const { return dependsOnStage; }
    /** Can this Output have more than one channel? */
    bool isListOutput() const { return _isList; }

    /** Output Interface */
    virtual void clearChannels() = 0;
    virtual void addChannel(const std::string& channelName) = 0;
    virtual const AbstractChannel& getChannel(const std::string& name) const = 0;
    // TODO other channel accessors.
    
    virtual std::string     getTypeName() const = 0;
    virtual std::string     getValueAsString(const SimTK::State& state) const = 0;
    virtual bool        isCompatible(const AbstractOutput&) const = 0;
    virtual void compatibleAssign(const AbstractOutput&) = 0;

    AbstractOutput& operator=(const AbstractOutput& o)
    { compatibleAssign(o); return *this; }

    virtual AbstractOutput* clone() const = 0;

    /** Specification for number of significant figures in string value. */
    unsigned int getNumberOfSignificantDigits() const { return _numSigFigs; }
    void         setNumberOfSignificantDigits(unsigned int numSigFigs) 
    { _numSigFigs = numSigFigs; }

protected:

    // Set the component that contains this Output.
    void setOwner(const Component& owner) {
        _owner.reset(&owner);
    }
    SimTK::ReferencePtr<const Component> _owner;

private:
    std::string name;
    SimTK::Stage dependsOnStage;
    unsigned int _numSigFigs = 8;
    bool _isList = false;

    // For calling setOwner().
    friend Component;
//=============================================================================
};  // END class AbstractOutput

template<class T>
class Output : public AbstractOutput {
public:

    class Channel;
    
    typedef std::unordered_map<std::string, Channel> ChannelMap;
    
    //default construct output function pointer and result container
    Output() {}
    /** Convenience constructor
    Create a Component::Output bound to a specific method of the Component and 
    valid at a given realization Stage.
    @param name             The name of the output.
    @param outputFunction   The output function to be invoked (returns Output T)
    @param dependsOnStage   Stage at which Output can be evaluated. */
    explicit Output(const std::string& name,
        const std::function<T (const Component* comp,
                               const SimTK::State&,
                               const std::string& channel)>& outputFunction,
        const SimTK::Stage&     dependsOnStage,
        bool                    isList) :
            AbstractOutput(name, dependsOnStage, isList),
            _outputFcn(outputFunction) {
        if (!isList) {
            // TODO rename.
            _channels["one"] = Channel(this, "one");
        }
    
    }
    
    /** Custom copy constructor is for setting the Channel's pointer
     * back to this Output. */
    Output(const Output& source) : AbstractOutput(source),
            _outputFcn(source._outputFcn), _channels(source._channels) {
        for (auto& it : _channels) {
            it.second._output.reset(this);
        }
    }
    
    /** Custom copy assignment operator is for setting the Channel's pointer
     * back to this Output. */
    Output& operator=(const Output& source) {
        if (&source == this) return *this;
        AbstractOutput::operator=(source);
        _outputFcn = source._outputFcn;
        _channels = source._channels;
        for (auto& it : _channels) {
            it.second._output.reset(this);
        }
        return *this;
    }
    
    virtual ~Output() {}
    
    // TODO someone more knowledgable could try to implement these.
    Output(Output&&) = delete;
    Output& operator=(Output&&) = delete;

    bool isCompatible(const AbstractOutput& o) const override { return isA(o); }
    void compatibleAssign(const AbstractOutput& o) override {
        if (!isA(o)) 
            SimTK_THROW2(SimTK::Exception::IncompatibleValues,
                         o.getTypeName(), getTypeName());
        *this = downcast(o);
    }
    
    void clearChannels() override {
        if (!isListOutput()) throw Exception("TODO");
        _channels.clear();
    }
    
    void addChannel(const std::string& channelName) override {
        if (!isListOutput()) throw Exception("TODO");
        _channels[channelName] = Channel(this, channelName);
    }
    
    const AbstractChannel& getChannel(const std::string& name) const override {
        return _channels.at(name);
    }
    
    const ChannelMap& getChannels() const { return _channels; }

    //--------------------------------------------------------------------------
    // OUTPUT VALUE
    //--------------------------------------------------------------------------
    /** Return the Value of this output if the state is appropriately realized   
        to a stage at or beyond the dependsOnStage, otherwise expect an
        Exception. */
    const T& getValue(const SimTK::State& state) const {
        _result = SimTK::NaN;
        if (isListOutput()) {
            throw Exception("TODO");
        }
        if (state.getSystemStage() < getDependsOnStage())
        {
            throw SimTK::Exception::StageTooLow(__FILE__, __LINE__,
                    state.getSystemStage(), getDependsOnStage(),
                    "Output::getValue(state)");
        }
        _result = _outputFcn(_owner.get(), state, "");
        return _result;
    }
    
    /** determine the value type for this Output*/
    std::string getTypeName() const override 
        { return SimTK::NiceTypeName<T>::namestr(); }

    std::string getValueAsString(const SimTK::State& state) const override {
        if (isListOutput()) {
            throw Exception("TODO");
        }
        unsigned int ns = getNumberOfSignificantDigits();
        std::stringstream s;
        s << std::setprecision(ns) << getValue(state);
        return s.str();
    }

    Output<T>* clone() const override { return new Output(*this); }
    SimTK_DOWNCAST(Output, AbstractOutput);

private:
    mutable T _result; // TODO remove
    std::function<T (const Component*,
                     const SimTK::State&,
                     const std::string& channel)> _outputFcn { nullptr };
    // TODO consider using indices, and having a parallel data structure
    // for names.
    std::unordered_map<std::string, Channel> _channels;

//=============================================================================
};  // END class Output


template <typename T>
class Output<T>::Channel : public AbstractChannel {
public:
    Channel() = default;
    Channel(const Output<T>* output, const std::string& name)
     : _output(output), _name(name) {}
    const T& getValue(const SimTK::State& state) const {
        // Must cache, since we're returning a reference.
        _result =_output->_outputFcn(_output->_owner.get(), state, _name);
        return _result;
    }
    const Output<T>& getOutput() const { return _output.getRef(); }
    const std::string& getName() const override { return _name; }
private:
    mutable T _result; // TODO remove
    SimTK::ReferencePtr<const Output<T>> _output;
    std::string _name;
    friend Output<T>::Output(const Output&);
    friend Output<T>& Output<T>::operator=(const Output&);
};

// TODO consider using std::reference_wrapper<T> as type for _output_##oname,
// since it is copyable.

/// @name Creating Outputs for your Component
/// Use these macros at the top of your component class declaration,
/// near where you declare @ref Property properties.
/// @{
/** Create an output for a member function of this component.
 *  The following must be true about componentMemberFunction, the function
 *  that returns the output:
 *
 *     -# It is a member function of your component.
 *     -# The member function is const.
 *     -# It takes only one input, which is `const SimTK::State&`
 *
 *  You must also provide the stage on which the output depends.
 *
 *  Here's an example for using this macro:
 *  @code{.cpp}
 *  class MyComponent : public Component {
 *  public:
 *      OpenSim_DECLARE_OUTPUT(force, double, getForce, SimTK::Stage::Dynamics);
 *      ...
 *  };
 *  @endcode
 * @see Component::constructOutput()
 * @relates OpenSim::Output
 */
#define OpenSim_DECLARE_OUTPUT(oname, T, func, ostage)                      \
    /** @name Outputs                                                    */ \
    /** @{                                                               */ \
    /** Provides the value of func##() and is available at stage ostage. */ \
    /** This output was generated with the                               */ \
    /** #OpenSim_DECLARE_OUTPUT macro.                                   */ \
    OpenSim_DOXYGEN_Q_PROPERTY(T, oname)                                    \
    /** @}                                                               */ \
    /** @cond                                                            */ \
    bool _has_output_##oname { constructOutput<T>(#oname, &Self::func, ostage) }; \
    /** @endcond                                                         */
    
#define OpenSim_DECLARE_LIST_OUTPUT(oname, T, func, ostage)                 \
    /** @name Outputs (list)                                             */ \
    /** @{                                                               */ \
    /** Provides the value of func##() and is available at stage ostage. */ \
    /** This output can have multiple channels. TODO                     */ \
    /** This output was generated with the                               */ \
    /** #OpenSim_DECLARE_LIST_OUTPUT macro.                              */ \
    OpenSim_DOXYGEN_Q_PROPERTY(T, oname)                                    \
    /** @}                                                               */ \
    /** @cond                                                            */ \
    bool _has_output_##oname { constructListOutput<T>(#oname, &Self::func, ostage) }; \
    /** @endcond                                                         */

// Note: we could omit the T argument from the above macro by using the
// following code to deduce T from the provided func
//      std::result_of<decltype(&Self::func)(Self, const SimTK::State&)>::type
// However, then we wouldn't be able to document the type for the output in
// doxygen.

/** Create an Output for a StateVariable in this component. The provided
 * name is both the name of the output and of the state variable.
 *
 * While this macro is a convenient way to construct an Output for a
 * StateVariable, it is inefficient because it uses a string lookup at runtime.
 * To create a more efficient Output, create a member variable that returns the
 * state variable directly (see Coordinate::getValue() or
 * Muscle::getActivation()) and then use the #OpenSim_DECLARE_OUTPUT macro.
 *
 * @code{.cpp}
 * class MyComponent : public Component {
 * public:
 *     OpenSim_DECLARE_OUTPUT_FOR_STATE_VARIABLE(activation);
 *     ...
 * };
 * @endcode
 * @see Component::constructOutputForStateVariable()
 * @relates OpenSim::Output
 */
#define OpenSim_DECLARE_OUTPUT_FOR_STATE_VARIABLE(oname)                    \
    /** @name Outputs                                                    */ \
    /** @{                                                               */ \
    /** Provides the value of this class's oname state variable.         */ \
    /** Available at stage SimTK::Stage::Model.                          */ \
    /** This output was generated with the                               */ \
    /** #OpenSim_DECLARE_OUTPUT_FOR_STATE_VARIABLE macro.                */ \
    OpenSim_DOXYGEN_Q_PROPERTY(double, oname)                               \
    /** @}                                                               */ \
    /** @cond                                                            */ \
    bool _has_output_##oname { constructOutputForStateVariable(#oname) };   \
    /** @endcond                                                         */
/// @}
//=============================================================================
//=============================================================================

} // end of namespace OpenSim

#endif  // OPENSIM_COMPONENT_OUTPUT_H_
