#include <pybind11/eigen.h>
#include <pybind11/operators.h>
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#include <string>

#include "cell.h"
#include "lagrange.h"
#include "nedelec.h"
#include "polynomial-set.h"
#include "quadrature.h"
#include "raviart-thomas.h"
#include "tp.h"

namespace py = pybind11;

PYBIND11_MODULE(libtab, m)
{
  m.doc() = "libtab plugin";

  py::enum_<Cell::Type>(m, "CellType")
      .value("interval", Cell::Type::interval)
      .value("triangle", Cell::Type::triangle)
      .value("tetrahedron", Cell::Type::tetrahedron)
      .value("quadrilateral", Cell::Type::quadrilateral)
      .value("hexahedron", Cell::Type::hexahedron)
      .value("prism", Cell::Type::prism)
      .value("pyramid", Cell::Type::pyramid);

  m.def("simplex_type", &Cell::simplex_type);
  m.def("create_lattice", &Cell::create_lattice);

  py::class_<Nedelec>(m, "Nedelec")
      .def(py::init<Cell::Type, int>())
      .def("tabulate", &Nedelec::tabulate);

  py::class_<Lagrange>(m, "Lagrange")
      .def(py::init<Cell::Type, int>())
      .def("tabulate", &Lagrange::tabulate);

  py::class_<TensorProduct>(m, "TensorProduct")
      .def(py::init<Cell::Type, int>())
      .def("tabulate", &TensorProduct::tabulate);

  py::class_<RaviartThomas>(m, "RaviartThomas")
      .def(py::init<Cell::Type, int>())
      .def("tabulate", &RaviartThomas::tabulate);

  m.def("tabulate_polynomial_set", &PolynomialSet::tabulate_polynomial_set);
  m.def("tabulate_polynomial_set_deriv",
        &PolynomialSet::tabulate_polynomial_set_deriv);

  m.def("compute_jacobi_deriv", &compute_jacobi_deriv);

  m.def("make_quadrature",
        py::overload_cast<const Eigen::Array<double, Eigen::Dynamic,
                                             Eigen::Dynamic, Eigen::RowMajor>&,
                          int>(&make_quadrature));
}
