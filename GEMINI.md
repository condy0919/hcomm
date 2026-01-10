# HCOMM C++ Gemini Collaboration Guide

This document outlines conventions and best practices for AI-assisted development in the HCOMM C++ codebase.

## Preferred Tools & Libraries

* HCOMM uses C++23, so that we can take the advantage of the modern C++.
* Prefer std library when available, use absl library when not

## Code Style & Conventions

* Only include headers that are actively used.
* Prefer explicit types over `std::pair` or `std::tuple` for return types.

## About HCOMM

* HCOMM is a high performance communication library for C/S applications by exposing hardware capability as
  much as possible, and also optimization of domain oriented applications.
* Various protocol supports, HCOMM hides the complication of low level API, TCP/RDMA/UB etc
