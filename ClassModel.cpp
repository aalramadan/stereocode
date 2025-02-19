// SPDX-License-Identifier: GPL-3.0-only
/**
 * @file ClassModel.cpp
 *
 * @copyright Copyright (C) 2021-2024 srcML, LLC. (www.srcML.org)
 *
 * This file is part of the Stereocode application.
 */

#include "ClassModel.hpp"

extern std::unordered_map
       <int, std::unordered_map
       <std::string, std::string>>   XPATH_LIST;   
extern primitiveTypes                PRIMITIVES;
extern int                           METHODS_PER_CLASS_THRESHOLD;
extern XPathBuilder                  XPATH_TRANSFORMATION;  

classModel::classModel(srcml_archive* archive, srcml_unit* unit, const std::string& unitLang) {
    unitLanguage = unitLang;
    findClassName(archive, unit);  
}

void classModel::findClassData(srcml_archive* archive, srcml_unit* unit, const std::string& classXpath, int unitNumber) {
    xpath[unitNumber].push_back(classXpath);
    if (unitLanguage == "C++") findStructureType(archive, unit); // Needed for findParentClassName()
    findParentClassName(archive, unit); // Requires structure type for C++
    
    std::vector<variable> attributeOrdered;
    int numOfCurrentAttributes = attributeOrdered.size(); // Used for partial classes
    findAttributeName(archive, unit, attributeOrdered);
    findAttributeType(archive, unit, attributeOrdered, numOfCurrentAttributes);
    
    // The "this" keyword by itself is assumed to be an "accessor" to the state of the class
    // It is also not a non-primitive
    variable v;
    v.setName("this");
    attributes.insert({v.getName(), v});
    
    std::vector<variable> nonPrivateAttributeOrdered; 
    int numOfCurrentNonPrivateAttributes = nonPrivateAttributeOrdered.size();
    findNonPrivateAttributeName(archive, unit, nonPrivateAttributeOrdered);
    findNonPrivateAttributeType(archive, unit, nonPrivateAttributeOrdered, numOfCurrentNonPrivateAttributes);
    findMethod(archive, unit, classXpath, unitNumber);

    if (unitLanguage == "C#") findMethodInProperty(archive, unit, classXpath, unitNumber); 
}


// Finds class name
//
void classModel::findClassName(srcml_archive* archive, srcml_unit* unit) {
    srcml_append_transform_xpath(archive, XPATH_TRANSFORMATION.getXpath(unitLanguage,"class_name").c_str());
    srcml_transform_result* result = nullptr;
    srcml_unit_apply_transforms(archive, unit, &result);

    if (srcml_transform_get_unit_size(result) == 1) {
        char *unparsed = nullptr;
        std::size_t size = 0;
        srcml_unit_unparse_memory(srcml_transform_get_unit(result, 0), &unparsed, &size);
        
        std::string tempName = unparsed;
        name.push_back(tempName); 

        trimWhitespace(tempName);
        name.push_back(tempName);
        
        std::size_t listOpen = tempName.find("<");
        if (listOpen != std::string::npos) {
            std::string nameLeft = tempName.substr(0, listOpen);
            std::string nameRight = tempName.substr(listOpen, tempName.size() - listOpen);
            removeBetweenComma(nameRight, true);
            removeNamespace(nameLeft, true, unitLanguage);
            name.push_back(nameLeft + nameRight);
            name.push_back(nameLeft);
        }
        else {
            removeNamespace(tempName, true, unitLanguage);
            name.push_back(tempName);
            name.push_back(tempName); // Not a duplicate
        }
        
        free(unparsed);     
    }

    // There might be a missing name (e.g., anonymous structs in C++)
    if (name.size() == 0) name = {"", "", "", ""}; 

    srcml_clear_transforms(archive);
    srcml_transform_free(result); 
}

// Determines the structure type (class, interface, or struct)
//
void classModel::findStructureType(srcml_archive* archive, srcml_unit* unit) {
    srcml_append_transform_xpath(archive, XPATH_TRANSFORMATION.getXpath(unitLanguage,"class_type").c_str());
    srcml_transform_result* result = nullptr;
    srcml_unit_apply_transforms(archive, unit, &result);

    if (srcml_transform_get_unit_size(result) == 1) {
        structureType = srcml_unit_get_srcml(srcml_transform_get_unit(result, 0));
        trimWhitespace(structureType);
    }
    
    srcml_clear_transforms(archive);
    srcml_transform_free(result); 
}

// Finds parent classes
// C++ supports multiple inheritance 
//  Classes and structs can inherit from each other
//  C++ doesn't support interfaces
//  C++ can use the public, private, and protected specifiers to control inheritance
//   It is private by default if nothing is specified for a class and public by default for a struct
//  C# and Java inheritance is always public
// Java and C# only support single inheritance from other classes and multiple inheritance from interfaces
//  Java doesn't support structs
//  Java interfaces can't inherit from classes
//  C# interfaces can't inherit from classes or structs
//  C# structs can't inherit from other structs or classes, but can inherit from interfaces
// C++ and C# uses ":" for inheritance
// Java uses 'extends' for class-to-class and interface-to-interface inheritance, 
//  and 'implements' for class-to-interface inheritance
// 
void classModel::findParentClassName(srcml_archive* archive, srcml_unit* unit) { 
    srcml_append_transform_xpath(archive, XPATH_TRANSFORMATION.getXpath(unitLanguage,"parent_name").c_str());
    srcml_transform_result* result = nullptr;
    srcml_unit_apply_transforms(archive, unit, &result);
    int n = srcml_transform_get_unit_size(result);

    srcml_unit* resultUnit = nullptr;
    for (int i = 0; i < n; i++) {
        resultUnit = srcml_transform_get_unit(result, i);

        char* unparsed = nullptr;
        std::size_t size = 0;
        srcml_unit_unparse_memory(resultUnit, &unparsed, &size);
        std::string parentName = unparsed;

        std::string inheritanceSpecifier;
        if (unitLanguage == "C++") {
            std::string temp = srcml_unit_get_srcml(resultUnit);
            if (temp.find("<specifier>public</specifier>") != std::string::npos) {
                inheritanceSpecifier = "public";
                parentName.erase(0, inheritanceSpecifier.size());  
            }             
            else if (temp.find("<specifier>protected</specifier>") != std::string::npos) {
                inheritanceSpecifier = "protected";
                parentName.erase(0, inheritanceSpecifier.size());  
            }  
            else if (temp.find("<specifier>private</specifier>") != std::string::npos) {
                inheritanceSpecifier = "private";
                parentName.erase(0, inheritanceSpecifier.size());  
            }             
            else if (structureType == "class")
                inheritanceSpecifier = "private";
            else
                inheritanceSpecifier = "public";         
            
        }
        trimWhitespace(parentName);

        std::size_t listOpen = parentName.find("<");
        if (listOpen != std::string::npos) {
            std::string parClassNameLeft = parentName.substr(0, listOpen);
            std::string parClassNameRight = parentName.substr(listOpen, parentName.size() - listOpen);
            removeNamespace(parClassNameLeft, true, unitLanguage); 
            parentClassName.insert({parClassNameLeft + parClassNameRight, inheritanceSpecifier});
        }
        else {
            removeNamespace(parentName, true, unitLanguage);
            parentClassName.insert({parentName, inheritanceSpecifier});
        }
    
        free(unparsed);      
    }
    
    srcml_clear_transforms(archive);
    srcml_transform_free(result); 
}

// Finds attribute names
// Only collect the name if there is a type
//
void classModel::findAttributeName(srcml_archive* archive, srcml_unit* unit, std::vector<variable>& attributeOrdered) {
    srcml_append_transform_xpath(archive, XPATH_TRANSFORMATION.getXpath(unitLanguage,"attribute_name").c_str());
    srcml_transform_result* result = nullptr;
    srcml_unit_apply_transforms(archive, unit, &result);
    int n = srcml_transform_get_unit_size(result);

    srcml_unit* resultUnit = nullptr;

    for (int i = 0; i < n; i++) {
        resultUnit = srcml_transform_get_unit(result,i);
        char* unparsed = nullptr;
        std::size_t size = 0;
        srcml_unit_unparse_memory(resultUnit, &unparsed, &size);     
        std::string attributeName = unparsed;

        variable v;

        // Chop off [] for arrays  
        if (unitLanguage == "C++") {
            std::size_t start_position = attributeName.find("[");
            if (start_position != std::string::npos){
                attributeName = attributeName.substr(0, start_position);
                Rtrim(attributeName);
            }
        }

        v.setName(attributeName);

        attributeOrdered.push_back(v); 
        free(unparsed);

    }
    srcml_clear_transforms(archive);
    srcml_transform_free(result);
}

// Finds attribute types
// Only collect the type if there is a name
//
void classModel::findAttributeType(srcml_archive* archive, srcml_unit* unit, std::vector<variable>& attributeOrdered, int numOfCurrentAttributes) {
    srcml_append_transform_xpath(archive, XPATH_TRANSFORMATION.getXpath(unitLanguage,"attribute_type").c_str());
    srcml_transform_result* result = nullptr;
    srcml_unit_apply_transforms(archive, unit, &result);
    int n = srcml_transform_get_unit_size(result);

    srcml_unit* resultUnit = nullptr;
    std::string prev; 

    for (int i = 0; i < n; ++i) {
        resultUnit = srcml_transform_get_unit(result, i);
        std::string type = srcml_unit_get_srcml(resultUnit);
        
        char* unparsed = nullptr;
        std::size_t size = 0;
        srcml_unit_unparse_memory(resultUnit, &unparsed, &size);
     
        if (type == "<type ref=\"prev\"/>") {
            type = prev;
        }
        else {  
            type = unparsed;
            prev = type;
        }

        attributeOrdered[numOfCurrentAttributes + i].setType(type);  
        attributes.insert({attributeOrdered[numOfCurrentAttributes + i].getName(), attributeOrdered[numOfCurrentAttributes + i]});
        bool nonPrimitiveAttributeExternal = false;

        isNonPrimitiveType(type, attributeOrdered[numOfCurrentAttributes + i], unitLanguage, name[3]);

        if (nonPrimitiveAttributeExternal)
            attributeOrdered[numOfCurrentAttributes + i].setNonPrimitiveExternal(true);
                          
        free(unparsed);
    }
    srcml_clear_transforms(archive);
    srcml_transform_free(result);
}

// Finds non-private attribute names
// For C++, no access specifier = private for a class, and public for a struct 
// For C#, no access specifier = private for a class, and public for a struct
//  Interfaces can't have attributes, only properties, which are public.
// For Java, no access specifier = accessible by derived classes (package-private) within the
//  same package (We will ignore this), and always public static for an interface
//  
void classModel::findNonPrivateAttributeName(srcml_archive* archive, srcml_unit* unit, std::vector<variable>& nonPrivateAttributeOrdered) {
    srcml_append_transform_xpath(archive, XPATH_TRANSFORMATION.getXpath(unitLanguage,"non_private_attribute_name").c_str());
    srcml_transform_result* result = nullptr;
    srcml_unit_apply_transforms(archive, unit, &result);
    int n = srcml_transform_get_unit_size(result);

    srcml_unit* resultUnit = nullptr;
    for (int i = 0; i < n; ++i) {
        resultUnit = srcml_transform_get_unit(result,i);
        char* unparsed = nullptr;
        std::size_t size = 0;
        srcml_unit_unparse_memory(resultUnit, &unparsed, &size); 
        std::string attributeName = unparsed;

        variable v;
        // Chop off [] for arrays  
        if (unitLanguage == "C++") {
            std::size_t start_position = attributeName.find("[");
            if (start_position != std::string::npos){
                attributeName = attributeName.substr(0, start_position);
                Rtrim(attributeName);
            }
        }
        
        v.setName(attributeName);

        nonPrivateAttributeOrdered.push_back(v); 
        free(unparsed);
    }
    srcml_clear_transforms(archive);
    srcml_transform_free(result);
}

// Finds non-private attribute types
//
void classModel::findNonPrivateAttributeType(srcml_archive* archive, srcml_unit* unit, std::vector<variable>& nonPrivateAttributeOrdered, 
                                             int numOfNonPrivateCurrentAttributes) {
    srcml_append_transform_xpath(archive, XPATH_TRANSFORMATION.getXpath(unitLanguage,"non_private_attribute_type").c_str());
    srcml_transform_result* result = nullptr;
    srcml_unit_apply_transforms(archive, unit, &result);
    int n = srcml_transform_get_unit_size(result);

    srcml_unit* resultUnit = nullptr;
    std::string prev;
    for (int i = 0; i < n; ++i) {
        resultUnit = srcml_transform_get_unit(result, i);
        std::string type = srcml_unit_get_srcml(resultUnit);
        char* unparsed = nullptr;
        std::size_t size = 0;
        srcml_unit_unparse_memory(resultUnit, &unparsed, &size);
     
        if (type == "<type ref=\"prev\"/>") {
            type = prev;
        }
        else {  
            type = unparsed;
            prev = type;
        }

        nonPrivateAttributeOrdered[numOfNonPrivateCurrentAttributes + i].setType(type);
        nonPrivateAndInheritedAttributes.insert({nonPrivateAttributeOrdered[numOfNonPrivateCurrentAttributes + i].getName(), 
                                                nonPrivateAttributeOrdered[numOfNonPrivateCurrentAttributes + i]});

        bool nonPrimitiveAttributeExternal = false;
        isNonPrimitiveType(type, nonPrivateAttributeOrdered[numOfNonPrivateCurrentAttributes + i], unitLanguage, name[3]);

        if (nonPrimitiveAttributeExternal)
            nonPrivateAttributeOrdered[numOfNonPrivateCurrentAttributes + i].setNonPrimitiveExternal(true);
        
        free(unparsed);
    }
    srcml_clear_transforms(archive);
    srcml_transform_free(result);
}

// Finds methods defined inside the class
//
void classModel::findMethod(srcml_archive* archive, srcml_unit* unit, const std::string& classXpath, int unitNumber) {
    srcml_append_transform_xpath(archive, XPATH_TRANSFORMATION.getXpath(unitLanguage,"method").c_str());
    srcml_transform_result* result = nullptr;
    srcml_unit_apply_transforms(archive, unit, &result);
    int n = srcml_transform_get_unit_size(result);    
    srcml_unit* resultUnit = nullptr;

    for (int i = 0; i < n; ++i) {
        resultUnit = srcml_transform_get_unit(result, i);

        srcml_archive* methodArchive = srcml_archive_create();
        srcml_archive_register_namespace(methodArchive, "pos", "http://www.srcML.org/srcML/position");
        char* unparsed = nullptr;
        std::size_t size = 0;
        srcml_archive_write_open_memory(methodArchive, &unparsed, &size);
        srcml_archive_write_unit(methodArchive, resultUnit);
        srcml_archive_close(methodArchive);
        srcml_archive_free(methodArchive);

        std::string methString = unparsed;
        srcmlBackwardCompatibility(methString);

        methodArchive = srcml_archive_create();
        srcml_archive_read_open_memory(methodArchive, methString.c_str(), methString.size());
        //srcml_archive_read_open_memory(methodArchive, unparsed, size);
        srcml_unit* methodUnit = srcml_archive_read_unit(methodArchive);

        std::string methodXpath = "(" + classXpath + XPATH_TRANSFORMATION.getXpath(unitLanguage,"method") + ")[" + std::to_string(i + 1) + "]";
        methodModel m = methodModel(methodArchive, methodUnit, methodXpath, unitLanguage, "", unitNumber);
        
        
        methods.push_back(m); 
        
        free(unparsed);       
        srcml_unit_free(methodUnit);
        srcml_archive_close(methodArchive);
        srcml_archive_free(methodArchive); 
    }
    srcml_clear_transforms(archive);
    srcml_transform_free(result);
}

// Properties need to be collected separately since they hold the return type of the getters
//
void classModel::findMethodInProperty(srcml_archive* archive, srcml_unit* unit, const std::string& classXpath, int unitNumber) {
    srcml_append_transform_xpath(archive, XPATH_TRANSFORMATION.getXpath(unitLanguage,"property").c_str());
    srcml_transform_result* result = nullptr;
    srcml_unit_apply_transforms(archive, unit, &result);
    int n = srcml_transform_get_unit_size(result);    

    for (int i = 0; i < n; ++i) {
        srcml_unit* resultUnit = srcml_transform_get_unit(result, i);

        srcml_archive* propertyArchive = srcml_archive_create();
        srcml_archive_register_namespace(propertyArchive, "pos", "http://www.srcML.org/srcML/position");
        char* unparsed = nullptr;
        std::size_t size = 0;
        srcml_archive_write_open_memory(propertyArchive, &unparsed, &size);
        srcml_archive_write_unit(propertyArchive, resultUnit);
        srcml_archive_close(propertyArchive);
        srcml_archive_free(propertyArchive);
        
        std::string propertyString = unparsed;
        srcmlBackwardCompatibility(propertyString);

        propertyArchive = srcml_archive_create();
        srcml_archive_read_open_memory(propertyArchive, propertyString.c_str(), propertyString.size());
        //srcml_archive_read_open_memory(propertyArchive, unparsed, size);;
        srcml_unit* propertyUnit = srcml_archive_read_unit(propertyArchive);

        srcml_append_transform_xpath(propertyArchive, XPATH_TRANSFORMATION.getXpath(unitLanguage,"property_type").c_str());
        srcml_transform_result* propertyResult = nullptr;
        srcml_unit_apply_transforms(propertyArchive, propertyUnit, &propertyResult);
        int nType = srcml_transform_get_unit_size(propertyResult);   
        if (nType > 0) {
            srcml_unit* typeUnit = srcml_transform_get_unit(propertyResult, 0);
            char* typeUnparsed = nullptr;
            std::size_t typeSize = 0;
            srcml_unit_unparse_memory(typeUnit, &typeUnparsed, &typeSize);

            srcml_transform_free(propertyResult);
            propertyResult = nullptr;
            srcml_clear_transforms(propertyArchive);

            srcml_append_transform_xpath(propertyArchive, XPATH_TRANSFORMATION.getXpath(unitLanguage,"property_method").c_str());
            srcml_unit_apply_transforms(propertyArchive, propertyUnit, &propertyResult);
            int nMethod = srcml_transform_get_unit_size(propertyResult); 
            for (int j = 0; j < nMethod; j++) {
                srcml_unit* methodResultUnit = srcml_transform_get_unit(propertyResult, j);

                srcml_archive* methodArchive = srcml_archive_create();
                srcml_archive_register_namespace(methodArchive, "pos", "http://www.srcML.org/srcML/position");
                char* methodUnparsed = nullptr;
                std::size_t methodSize = 0;
                srcml_archive_write_open_memory(methodArchive, &methodUnparsed, &methodSize);
                srcml_archive_write_unit(methodArchive, methodResultUnit);
                srcml_archive_close(methodArchive);
                srcml_archive_free(methodArchive);
            
                methodArchive = srcml_archive_create();
                srcml_archive_read_open_memory(methodArchive, methodUnparsed, methodSize);
                srcml_unit* methodUnit = srcml_archive_read_unit(methodArchive);

                std::string methodXpath = "((" + classXpath + XPATH_TRANSFORMATION.getXpath(unitLanguage,"property") + ")[" + std::to_string(i + 1) + "]";
                methodXpath += "//src:function)[" + std::to_string(j + 1) + "]";

                methodModel m = methodModel(methodArchive, methodUnit, methodXpath, unitLanguage, typeUnparsed, unitNumber);

                methods.push_back(m); 

                free(methodUnparsed);       
                srcml_unit_free(methodUnit);
                srcml_archive_close(methodArchive);
                srcml_archive_free(methodArchive); 
            }
            free(typeUnparsed);
        }

        
        free(unparsed);       
        srcml_unit_free(propertyUnit);
        srcml_transform_free(propertyResult);
        srcml_clear_transforms(propertyArchive);
        srcml_archive_close(propertyArchive);
        srcml_archive_free(propertyArchive); 
    }
    srcml_clear_transforms(archive);
    srcml_transform_free(result);
}

// Compute class stereotype
//  Based on definition from Dragan, Collard, Maletic ICSM 2010
// Constructors and destructors are not considered in the computation of class stereotypes
// 
void classModel::computeClassStereotype() {
    std::unordered_map<std::string, int> methodStereotypes = {
        {"get", 0},
        {"predicate", 0},
        {"property", 0},
        {"void-accessor", 0},
        {"set", 0},
        {"command", 0},
        {"non-void-command", 0},
        {"collaborator", 0},
        {"controller", 0},
        {"wrapper", 0},
        {"factory", 0},
        {"incidental", 0},
        {"stateless", 0},  
        {"empty", 0},
        {"unclassified", 0},
    };
    int nonCollaborators = 0;
    for (const auto& m : methods) {      
        if (!m.IsConstructorDestructorUsed()) {
            for (const std::string& s : m.getStereotypeList()) 
                methodStereotypes[s]++;
        
            std::string methodStereotype = m.getStereotype();
            if (methodStereotype.find("collaborator") == std::string::npos &&
                methodStereotype.find("controller") == std::string::npos && 
                methodStereotype.find("wrapper") == std::string::npos)
                    nonCollaborators++;
        }
    }

    int getters = methodStereotypes["get"];
    int accessors = getters + methodStereotypes["predicate"] +
                    methodStereotypes["property"] +
                    methodStereotypes["void-accessor"]; 

    int setters = methodStereotypes["set"];     
    int commands = methodStereotypes["command"] + methodStereotypes["non-void-command"];           
    int mutators = setters + commands;

    int controllers = methodStereotypes["controller"];
    int collaborator =  methodStereotypes["collaborator"] + methodStereotypes["wrapper"]; 
    int collaborators = controllers + collaborator;
    
    int factory = methodStereotypes["factory"];

    int degenerates = methodStereotypes["incidental"] + methodStereotypes["stateless"] + methodStereotypes["empty"];

    int allMethods = methods.size() - constructorDestructorCount;

    // Entity
    if (((accessors - getters) != 0) && ((mutators - setters)  != 0) ) {
        double ratio = double(collaborators) / double(nonCollaborators);
        if (ratio >= 2 && controllers == 0) 
            stereotype.push_back("entity");   
    }

    // Minimal Entity
    if (((allMethods - (getters + setters + commands)) == 0) && (getters != 0) && (setters != 0) & (commands != 0)) {
        double ratio = double(collaborators) / double(nonCollaborators);
        if (ratio >= 2) 
            stereotype.push_back("minimal-entity");   
    }

    // Data Provider
    if ((accessors > 2 * mutators) && (accessors > 2 * (controllers + factory)) )
        stereotype.push_back("data-provider");

    // Commander
    if ((mutators > 2 * accessors) && (mutators > 2 * (controllers + factory)))
        stereotype.push_back("commander");

    // Boundary
    if ((collaborators > nonCollaborators) && (factory < 0.5 * allMethods) && (controllers < 0.33 * allMethods))
        stereotype.push_back("boundary");

    // Factory
    if (factory > 0.67 * allMethods)
        stereotype.push_back("factory");
    
    // Controller
    if ((controllers + factory > 0.67 * allMethods) && ((accessors != 0) || (mutators != 0)))
        stereotype.push_back("controller");

    // Pure Controller
    if ((controllers + factory != 0) && ((accessors + mutators + collaborator) == 0) && (controllers != 0)) 
        stereotype.push_back("pure-controller");

    // Large Class
    {
        int accPlusMut = accessors + mutators;
        int facPlusCon = controllers + factory;
        if (((0.2 * allMethods < accPlusMut) && (accPlusMut < 0.67 * allMethods )) &&
            ((0.2 * allMethods < facPlusCon) && (facPlusCon < 0.67 * allMethods )) &&
            (factory != 0) && (controllers != 0) && (accessors != 0) && (mutators != 0) ) {
                if (allMethods > METHODS_PER_CLASS_THRESHOLD) { 
                    stereotype.push_back("large-class");
            }
        }
    }

    // Lazy Class
    if ((getters + setters != 0) && (((degenerates / double(allMethods)) > 0.33)) &&
       (((allMethods - (degenerates + getters + setters)) / double(allMethods))  <= 0.2))
        stereotype.push_back("lazy-class");
    
    // Degenerate Class
    if ((degenerates / double(allMethods)) > 0.5)  
        stereotype.push_back("degenerate");
    
    // Data Class
    if ((allMethods - (getters + setters) == 0) && ((getters + setters) != 0))
         stereotype.push_back("data-class");
    
    // Small Class
    if ((0 < allMethods) && (allMethods < 3))
        stereotype.push_back("small-class");

    // Empty Class (Considered degenerate)
    if (allMethods == 0)
        stereotype.push_back("empty");

    // Final check if no stereotype was assigned
    if (stereotype.size() == 0) 
        stereotype.push_back("unclassified");

    for (const auto& pair : xpath) 
        for (const auto& classXpath : pair.second) 
            XPATH_LIST[pair.first].insert({classXpath, getStereotype()});
           
}

//Compute method stereotypes
//
void classModel::computeMethodStereotype() {
    constructorDestructor();
    getter();
    predicate();
    property();
    voidAccessor(); 
    setter();
    command();
    factory();
    wrapperControllerCollaborator();
    incidental();
    stateless();
    empty();
    for (auto& m : methods) { 
        if (m.getStereotypeList().size() == 0) 
             m.setStereotype("unclassified");
        XPATH_LIST[m.getUnitNumber()].insert({m.getXpath(), m.getStereotype()});    
    }
}

// Stereotype constructor copy-constructor destructor:
//
void classModel::constructorDestructor() {
    for (auto& m : methods) {
        if (m.IsConstructorDestructorUsed()) {  
            constructorDestructorCount++;
            const std::string& parameterList = m.getParametersList();
            const std::string& srcML = m.getSrcML();

            if (srcML.find("<destructor>") != std::string::npos)
                m.setStereotype("destructor"); 
            else if (parameterList.find(name[3]) != std::string::npos) 
                m.setStereotype("copy-constructor");
            else 
                m.setStereotype("constructor");
        }
    }
}

// Stereotype get:
// 1] Return type is not void
// 2] Contains at least one simple return expression that returns an attribute (e.g., return dm;)
// "this" keyword by itself is not considered (e.g., return this;)
//
void classModel::getter() {
    for (auto& m : methods) {
        if (!m.IsConstructorDestructorUsed()) {  
            if (m.IsAttributeReturned()) {
                m.setStereotype("get");
            }
        }
    }
}

// Stereotype predicate:
// 1] Return type is Boolean
// 2] Contains at least one complex return expression
// 3] Uses a data member in an expression or has at least 
//     one function call to other methods in class
//
// Constructor calls are not considered
// Ignored calls are not considered
// "this" keyword by itself is considered
// 
void classModel::predicate() { 
    for (auto& m : methods) {
        if (!m.IsConstructorDestructorUsed()) {  
            bool returnType = false;
            const std::string& returnTypeParsed = m.getReturnTypeParsed();

            if (unitLanguage == "C++")
                returnType = (returnTypeParsed == "bool");
            else if (unitLanguage == "C#")
                returnType = (returnTypeParsed == "bool") || 
                            (returnTypeParsed == "Boolean");
            else if (unitLanguage == "Java")
                returnType = (returnTypeParsed == "boolean");

            bool hasComplexReturnExpr = m.IsAttributeNotReturned();
            bool isAttributeUsed = m.IsAttributeUsed();
            bool callsToOtherClassMethods = m.getFunctionCalls().size() > 0;

            if (returnType && hasComplexReturnExpr && (isAttributeUsed || callsToOtherClassMethods))
                m.setStereotype("predicate"); 
        }
    }
}

// Stereotype property:
// 1] Return type is not void or Boolean
// 2] Contains at least one complex return statement (e.g., return a+5;)
// 3] Uses a data member in an expression or has at least 
//     one function call to other methods in class
//
// Constructor calls are not considered
// Ignored calls are not considered
// "this" keyword by itself is considered
//  
void classModel::property() {
    for (auto& m : methods) {
        if (!m.IsConstructorDestructorUsed() && !m.IsStrictFactory()) {  
            bool returnNotVoidOrBool = false;
            const std::string& returnTypeParsed = m.getReturnTypeParsed();


            bool isVoidPointer = false;
            if (unitLanguage != "Java") {  
                if (m.getReturnType().find("void*") != std::string::npos)
                    isVoidPointer = true;
            }

            if (unitLanguage == "C++")
                returnNotVoidOrBool = (returnTypeParsed != "bool" && returnTypeParsed != "void" && returnTypeParsed != "") || isVoidPointer;
            else if (unitLanguage == "C#")
                returnNotVoidOrBool = (returnTypeParsed != "bool" && returnTypeParsed != "Boolean" &&
                            returnTypeParsed != "void" && returnTypeParsed != "Void" && returnTypeParsed != "") || isVoidPointer;
            else if (unitLanguage == "Java")
                returnNotVoidOrBool = (returnTypeParsed != "boolean" && returnTypeParsed != "void" && 
                            returnTypeParsed != "Void" && returnTypeParsed != "");

            bool isAttributeUsed = m.IsAttributeUsed();
            bool callsToOtherClassMethods = m.getFunctionCalls().size() > 0;

            if (returnNotVoidOrBool && m.IsAttributeNotReturned() && (isAttributeUsed || callsToOtherClassMethods)) {
                m.setStereotype("property");
            }
        }
    }
}

// Stereotype void-accessor:
// 1] Return type is void 
// 2] Contains at least one parameter that is passed by non-const reference and is assigned a value
// 3] Uses a data member in an expression or has at least 
//     one function call to other methods in class 
//
// Constructor calls are not considered
// Ignored calls are not considered
// "this" keyword by itself is considered
//
void classModel::voidAccessor() {
    for (auto& m : methods) {
        if (!m.IsConstructorDestructorUsed()) {  
            bool isAttributeUsed = m.IsAttributeUsed();
            bool callsToOtherClassMethods = m.getFunctionCalls().size() > 0;

            bool isVoidPointer = false;
            if (unitLanguage != "Java") {              
                if (m.getReturnType().find("void*") != std::string::npos)
                    isVoidPointer = true;
            }

            bool returnVoid = m.getReturnTypeParsed() == "void";

            if (m.IsParameterRefChanged() && returnVoid && !isVoidPointer && 
               (isAttributeUsed || callsToOtherClassMethods)) {
                m.setStereotype("void-accessor");       
            } 
        }
    }
}

// Stereotype set:
// 1] Only one data member is changed
// 2] Number of calls on data members or to methods in class is at most 1
//
// Constructor calls are not considered
// Ignored calls are not considered
// "this" keyword by itself is considered
//
void classModel::setter() {
    for (auto& m : methods) {
        if (!m.IsConstructorDestructorUsed()) {  
            bool oneAttributeModified = m.getNumOfAttributesModified() == 1;
            int callsToClassMethodsOrOnAttributes = m.getFunctionCalls().size() + m.getMethodCalls().size();
            
            if (oneAttributeModified && (callsToClassMethodsOrOnAttributes <= 1)) 
                m.setStereotype("set"); 
        }   
    }
}

// stereotype command:
//     Method has a void return type
//     Cases:
//          Case 1: More than one data member is modifed
//          Case 2: one data member is modifed and:
//                  -	There is at least two calls on data members or
//                      function calls to other methods in class
//          Case 3: zero data members modifed and:
//                  -	there is at least one call on a data member
//                      or one function call to other methods in clasa
//     Method is not const (C++ only)
//     Case 1 applies when attributes are mutable and method is const (C++ only)
//
//    Constructor calls are not considered
//    Ignored calls are not considered
//    "this" keyword by itself is considered
//
// stereotype non-void-command (C++ only):        
//    Method return type is not void
//
void classModel::command() {
    for (auto& m : methods) {
        if (!m.IsConstructorDestructorUsed()) {  
            const std::string& returnTypeParsed = m.getReturnTypeParsed();
            int attributeModified = m.getNumOfAttributesModified();
            int callsToMethodsInClass = m.getFunctionCalls().size() ; 
            int callsOnDataMembers = m.getMethodCalls().size();

            bool case1 = attributeModified == 0 && (callsToMethodsInClass > 0 || callsOnDataMembers > 0);
            bool case2 = attributeModified == 1 && ((callsOnDataMembers + callsToMethodsInClass) > 1);
            bool case3 = attributeModified > 1;
            bool mutableCase = m.IsConstMethod() && case3;
 
            bool isVoidPointer = false;
            if (unitLanguage != "Java") {  
                if (m.getReturnType().find("void*") != std::string::npos)
                    isVoidPointer = true;
            }
            
            bool returnCheck = returnTypeParsed != "void" && returnTypeParsed != "Void" && !isVoidPointer;

            if (case1 || case2 || case3) {
                if (!m.IsConstMethod() || mutableCase){ // Handles case of mutable attributes (C++ only)
                    if (returnCheck)
                        m.setStereotype("non-void-command");  
                    else
                        m.setStereotype("command");
                }
            } 
        }
    }
}

// Stereotype factory
// 1] Factories must include a non-primitive type in their return type
//      and their return expression must be a local variable, parameter, or attribute, that 
//      call a constructor call or has a return expression with a constructor call (e.g., new)
//
// Variables created with ignored calls are considered
// Returns that have "new" ignored calls are also considered
// "this" keyword by itself is not considered
//
void classModel::factory() {
    for (auto& m : methods) {
        if (m.IsFactory() || m.IsStrictFactory()) m.setStereotype("factory");
    }
}
// Stereotype wrapper:
// 1] No data members are modified
// 2] No calls to methods in class
// 3] No calls on data members
// 4] Has at least one free function call 
// Constructor calls are not considered
//
// Stereotype controller:
// 1] No data members are modified
// 2] No calls to methods in class
// 3] No calls on data members
// 3] Has at least one call to other class methods or mutates a parameter or a local that is non-primitive
//
// Stereotype collaborator:
// 1] It must use at least 1 non-primitive type (not of this class)
// 2] Type could be a parameter, local variable, return type, or an attribute 
//
// Ignored calls are not considered
// "this" keyword by itself is considered only for wrapper and controller
//
void classModel::wrapperControllerCollaborator() {
    for (auto& m : methods) {
        if (!m.IsConstructorDestructorUsed()) {  
            if (!m.IsEmpty()){
                bool nonPrimitiveAttributeExternal = m.IsNonPrimitiveAttributeExternal();
                bool nonPrimitiveLocalExternal = m.IsNonPrimitiveLocalExternal();
                bool nonPrimitiveParamaterExternal = m.IsNonPrimitiveParamaterExternal();
                bool nonPrimitiveReturnExternal = m.IsNonPrimitiveReturnTypeExternal();
            
                bool isVoidPointer = false;
                if (unitLanguage != "Java") {  
                    if (m.getReturnType().find("void*") != std::string::npos )
                        isVoidPointer = true;
                }

                bool returnCheck = nonPrimitiveReturnExternal || isVoidPointer;

                bool noAttributeModified = m.getNumOfAttributesModified() == 0;
                bool noCallsToMethodsInClass = m.getFunctionCalls().size() == 0; 
                bool noCallsOnDataMembers = m.getMethodCalls().size() == 0;
                bool hasFreeFunctionCalls = m.getNumOfExternalFunctionCalls() > 0;
                bool hasCallsToOtherClassMethods = m.getNumOfExternalMethodCalls() > 0;
        
                if (noAttributeModified && noCallsToMethodsInClass && noCallsOnDataMembers && !hasCallsToOtherClassMethods && hasFreeFunctionCalls)
                    m.setStereotype("wrapper");

                else if (noAttributeModified && noCallsToMethodsInClass && noCallsOnDataMembers &&
                        (hasCallsToOtherClassMethods || m.IsNonPrimitiveLocalOrParameterChanged()))
                    m.setStereotype("controller");   

                else if (nonPrimitiveAttributeExternal || nonPrimitiveLocalExternal || nonPrimitiveParamaterExternal || returnCheck )
                    m.setStereotype("collaborator");   
            }
        }
    }
}

// Stereotype incidental 
// 1] Method contains at least one non-comment statement (i.e., method is not empty)
// 2] No data members are used or modified (including no use of keyword "this" by itself)
// 3] No calls of any kind
// Ignored calls are allowed
// 
void classModel::incidental() {
    for (auto& m : methods ) {
        if (!m.IsConstructorDestructorUsed() && !m.IsEmpty()) {  
            bool noCalls = m.getFunctionCalls().size() == 0 && m.getMethodCalls().size() == 0 && m.getConstructorCalls().size() == 0 &&
                           m.getNumOfExternalMethodCalls() == 0 && m.getNumOfExternalFunctionCalls() == 0;

            if (!m.IsAttributeUsed() && noCalls)
                m.setStereotype("incidental");         
            
        }
    }
}

// Stereotype stateless
// 1]	Method contains at least one non-comment statement (i.e., method is not empty)
// 2]	No data members are used or modified (including no use of keyword "this" by itself)
// 3]	No calls to methods in class 
// 4]   No calls on data members
// 5]   Has at least one call to other class methods (including constructor calls) or to a free function 
// Ignored calls are not considered
//
void classModel::stateless() {
    for (auto& m : methods) {
        if (m.getName() == "InitializeWithWindow")
            std::cout << "yes";
        if (!m.IsConstructorDestructorUsed()) {  
            if (!m.IsEmpty()) {
                bool noCallsToClassMethodsOrOnAttributes = m.getFunctionCalls().size() == 0 && m.getMethodCalls().size() == 0;
                bool hasFreeFunctionCalls = m.getNumOfExternalFunctionCalls() > 0;
                bool hasCallsToOtherClassMethods = m.getNumOfExternalMethodCalls() > 0; 
                bool hasConstructorCalls = m.getConstructorCalls().size() > 0;

                if (!m.IsAttributeUsed() && noCallsToClassMethodsOrOnAttributes &&
                (hasFreeFunctionCalls || hasCallsToOtherClassMethods || hasConstructorCalls))
                    m.setStereotype("stateless");             
            }
        }
    }
}

// Stereotype empty
// 1] Method has no statements except for comments
//
void classModel::empty() {
    for (auto& m : methods) {
        if (!m.IsConstructorDestructorUsed()) {  
            if (m.IsEmpty()) 
                m.setStereotype("empty");
        }
    }
}

std::string classModel::getStereotype () const {
    std::string result;

    for (const std::string &value : stereotype)
        result += value + " ";

    Rtrim(result);
     
    return result;
}
