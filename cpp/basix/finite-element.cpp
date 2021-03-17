// Copyright (c) 2020 Chris Richardson & Matthew Scroggs
// FEniCS Project
// SPDX-License-Identifier:    MIT

#include "finite-element.h"
#include "brezzi-douglas-marini.h"
#include "bubble.h"
#include "crouzeix-raviart.h"
#include "lagrange.h"
#include "nce-rtc.h"
#include "nedelec.h"
#include "polyset.h"
#include "raviart-thomas.h"
#include "regge.h"
#include "serendipity.h"
#include <numeric>

#include <xtensor-blas/xlinalg.hpp>
#include <xtensor/xadapt.hpp>
#include <xtensor/xbuilder.hpp>
#include <xtensor/xlayout.hpp>
#include <xtensor/xview.hpp>

#define str_macro(X) #X
#define str(X) str_macro(X)

using namespace basix;

//-----------------------------------------------------------------------------
basix::FiniteElement basix::create_element(std::string family, std::string cell,
                                           int degree)
{
  return basix::create_element(element::str_to_type(family),
                               cell::str_to_type(cell), degree);
}
//-----------------------------------------------------------------------------
basix::FiniteElement basix::create_element(element::family family,
                                           cell::type cell, int degree)
{
  switch (family)
  {
  case element::family::P:
    return create_lagrange(cell, degree);
  case element::family::DP:
    return create_dlagrange(cell, degree);
  case element::family::BDM:
    return create_bdm(cell, degree);
  case element::family::RT:
  {
    switch (cell)
    {
    case cell::type::quadrilateral:
      return create_rtc(cell, degree);
    case cell::type::hexahedron:
      return create_rtc(cell, degree);
    default:
      return create_rt(cell, degree);
    }
  }
  case element::family::N1E:
  {
    switch (cell)
    {
    case cell::type::quadrilateral:
      return create_nce(cell, degree);
    case cell::type::hexahedron:
      return create_nce(cell, degree);
    default:
      return create_nedelec(cell, degree);
    }
  }
  case element::family::N2E:
    return create_nedelec2(cell, degree);
  case element::family::Regge:
    return create_regge(cell, degree);
  case element::family::CR:
    return create_cr(cell, degree);
  case element::family::Bubble:
    return create_bubble(cell, degree);
  case element::family::Serendipity:
    return create_serendipity(cell, degree);
  case element::family::DPC:
    return create_dpc(cell, degree);
  default:
    throw std::runtime_error("Family not found");
  }
}
//-----------------------------------------------------------------------------
xt::xtensor<double, 2> basix::compute_expansion_coefficients(
    cell::type celltype, const xt::xtensor<double, 2>& B,
    const xt::xtensor<double, 2>& M, const xt::xtensor<double, 2>& x,
    int degree, double kappa_tol)
{
  // TODO: Tidy up 1D views for 1D problems
  xt::xarray<double> pts = x;
  if (pts.shape()[1] == 1)
    pts.reshape({pts.shape()[0]});

  const xt::xtensor<double, 3> P = polyset::tabulate(celltype, degree, 0, pts);

  const int coeff_size = P.shape()[2];
  const int value_size = B.shape()[1] / coeff_size;
  const int m_size = M.shape()[1] / value_size;
  xt::xtensor<double, 2> A = xt::zeros<double>({B.shape()[0], M.shape()[0]});
  for (std::size_t row = 0; row < B.shape()[0]; ++row)
  {
    for (int v = 0; v < value_size; ++v)
    {
      auto Bview
          = xt::view(B, row, xt::range(v * coeff_size, (v + 1) * coeff_size));
      auto Mview_t
          = xt::view(M, xt::all(), xt::range(v * m_size, (v + 1) * m_size));

      // Compute Aview = Bview * Pt * Mview ( Aview_i = Bview_j * Pt_jk
      // * Mview_ki )
      for (std::size_t i = 0; i < A.shape()[1]; ++i)
        for (std::size_t k = 0; k < P.shape()[1]; ++k)
          for (std::size_t j = 0; j < P.shape()[2]; ++j)
            A(row, i) += Bview(j) * P(0, k, j) * Mview_t(i, k);
    }
  }

  if (kappa_tol >= 1.0)
  {
    if (xt::linalg::cond(A, 2) > kappa_tol)
    {
      throw std::runtime_error("Condition number of B.D^T when computing "
                               "expansion coefficients exceeds tolerance.");
    }
  }

  return xt::linalg::solve(A, B);
}
//-----------------------------------------------------------------------------
std::pair<xt::xtensor<double, 2>, xt::xtensor<double, 2>>
basix::combine_interpolation_data(const xt::xtensor<double, 2>& points_1d,
                                  const xt::xtensor<double, 2>& points_2d,
                                  const xt::xtensor<double, 2>& points_3d,
                                  const xt::xtensor<double, 2>& matrix_1d,
                                  const xt::xtensor<double, 2>& matrix_2d,
                                  const xt::xtensor<double, 2>& matrix_3d,
                                  std::size_t tdim, std::size_t value_size)
{
  std::array<std::size_t, 3> num_ptsd
      = {points_1d.shape()[0], points_2d.shape()[0], points_3d.shape()[0]};
  std::size_t num_pts = std::accumulate(num_ptsd.begin(), num_ptsd.end(), 0);
  xt::xtensor<double, 2> points({num_pts, tdim});

  if (num_ptsd[0] > 0)
    xt::view(points, xt::range(0, num_ptsd[0]), xt::all()) = points_1d;

  if (num_ptsd[1] > 0)
  {
    xt::view(points, xt::range(num_ptsd[0], num_ptsd[0] + num_ptsd[1]),
             xt::all())
        = points_2d;
  }

  if (num_ptsd[2] > 0)
  {
    auto range = xt::range(num_ptsd[0] + num_ptsd[1],
                           num_ptsd[0] + num_ptsd[1] + num_ptsd[2]);
    xt::view(points, range, xt::all()) = points_3d;
  }

  std::array<std::size_t, 3> row_dim
      = {matrix_1d.shape()[0], matrix_2d.shape()[0], matrix_3d.shape()[0]};
  std::size_t num_rows = std::accumulate(row_dim.begin(), row_dim.end(), 0);
  std::array<std::size_t, 3> col_dim
      = {matrix_1d.shape()[1], matrix_2d.shape()[1], matrix_3d.shape()[1]};
  std::size_t num_cols = std::accumulate(col_dim.begin(), col_dim.end(), 0);

  xt::xtensor<double, 2> matrix = xt::zeros<double>({num_rows, num_cols});
  std::transform(col_dim.begin(), col_dim.end(), col_dim.begin(),
                 [value_size](auto x) { return x /= value_size; });
  num_cols /= value_size;
  for (std::size_t i = 0; i < value_size; ++i)
  {
    {
      auto range0 = xt::range(0, row_dim[0]);
      auto range1 = xt::range(i * num_cols, i * num_cols + col_dim[0]);
      auto range = xt::range(i * col_dim[0], i * col_dim[0] + col_dim[0]);
      xt::view(matrix, range0, range1) = xt::view(matrix_1d, xt::all(), range);
    }

    {
      auto range0 = xt::range(row_dim[0], row_dim[0] + row_dim[1]);
      auto range1 = xt::range(i * num_cols + col_dim[0],
                              i * num_cols + col_dim[0] + col_dim[1]);
      auto range = xt::range(i * col_dim[1], i * col_dim[1] + col_dim[1]);
      xt::view(matrix, range0, range1) = xt::view(matrix_2d, xt::all(), range);
    }

    {
      auto range0 = xt::range(row_dim[0] + row_dim[1],
                              row_dim[0] + row_dim[1] + row_dim[2]);
      auto range1
          = xt::range(i * num_cols + col_dim[0] + col_dim[1],
                      i * num_cols + col_dim[0] + col_dim[1] + col_dim[2]);
      auto range = xt::range(i * col_dim[2], i * col_dim[2] + col_dim[2]);
      xt::view(matrix, range0, range1) = xt::view(matrix_3d, xt::all(), range);
    }
  }

  return std::make_pair(points, matrix);
}
//-----------------------------------------------------------------------------
FiniteElement::FiniteElement(element::family family, cell::type cell_type,
                             int degree,
                             const std::vector<std::size_t>& value_shape,
                             const xt::xtensor<double, 2>& coeffs,
                             const std::vector<std::vector<int>>& entity_dofs,
                             const xt::xtensor<double, 3>& base_transformations,
                             const xt::xtensor<double, 2>& points,
                             const xt::xtensor<double, 2>& M,
                             mapping::type map_type)
    : map_type(map_type), _cell_type(cell_type), _family(family),
      _degree(degree), _map_type(map_type), _coeffs(coeffs),
      _entity_dofs(entity_dofs), _base_transformations(base_transformations),
      _matM(M)
{
  if (points.dimension() == 1)
    throw std::runtime_error("Problem with points");
  _points = points;

  _value_shape = std::vector<int>(value_shape.begin(), value_shape.end());

  // Check that entity dofs add up to total number of dofs
  std::size_t sum = 0;
  for (const std::vector<int>& q : entity_dofs)
    sum = std::accumulate(q.begin(), q.end(), sum);

  if (sum != _coeffs.shape()[0])
  {
    throw std::runtime_error(
        "Number of entity dofs does not match total number of dofs");
  }
}
//-----------------------------------------------------------------------------
cell::type FiniteElement::cell_type() const { return _cell_type; }
//-----------------------------------------------------------------------------
int FiniteElement::degree() const { return _degree; }
//-----------------------------------------------------------------------------
int FiniteElement::value_size() const
{
  int value_size = 1;
  for (int d : _value_shape)
    value_size *= d;
  return value_size;
}
//-----------------------------------------------------------------------------
const std::vector<int>& FiniteElement::value_shape() const
{
  return _value_shape;
}
//-----------------------------------------------------------------------------
int FiniteElement::dim() const { return _coeffs.shape()[0]; }
//-----------------------------------------------------------------------------
element::family FiniteElement::family() const { return _family; }
//-----------------------------------------------------------------------------
mapping::type FiniteElement::mapping_type() const { return _map_type; }
//-----------------------------------------------------------------------------
const xt::xtensor<double, 2>& FiniteElement::interpolation_matrix() const
{
  return _matM;
}
//-----------------------------------------------------------------------------
const std::vector<std::vector<int>>& FiniteElement::entity_dofs() const
{
  return _entity_dofs;
}
//-----------------------------------------------------------------------------
xt::xtensor<double, 3>
FiniteElement::tabulate_new(int nd, const xt::xarray<double>& x) const
{
  const std::size_t tdim = cell::topological_dimension(_cell_type);
  std::size_t ndsize = 1;
  for (int i = 1; i <= nd; ++i)
    ndsize *= (tdim + i);
  for (int i = 1; i <= nd; ++i)
    ndsize /= i;
  const std::size_t vs = value_size();
  const std::size_t ndofs = _coeffs.shape()[0];

  xt::xarray<double> _x = x;
  if (_x.dimension() == 1)
    _x.reshape({_x.shape(0), 1});

  std::vector<double> basis_data(ndsize * x.shape()[0] * ndofs * vs);
  tabulate(nd, _x, basis_data.data());

  xt::xtensor<double, 3> d({ndsize, _x.shape()[0], ndofs * vs});
  for (std::size_t i = 0; i < d.shape(0); ++i)
  {
    std::size_t offset = i * x.shape()[0] * ndofs * vs;
    std::array<std::size_t, 2> shape = {_x.shape()[0], ndofs * vs};
    auto mat = xt::adapt<xt::layout_type::column_major>(
        basis_data.data() + offset, x.shape()[0] * ndofs * vs,
        xt::no_ownership(), shape);
    xt::view(d, i, xt::all(), xt::all()) = mat;
  }

  return d;
}
//-----------------------------------------------------------------------------
xt::xtensor<double, 4>
FiniteElement::tabulate_x(int nd, const xt::xarray<double>& x) const
{
  const std::size_t tdim = cell::topological_dimension(_cell_type);
  std::size_t ndsize = 1;
  for (int i = 1; i <= nd; ++i)
    ndsize *= (tdim + i);
  for (int i = 1; i <= nd; ++i)
    ndsize /= i;
  const std::size_t vs = value_size();
  const std::size_t ndofs = _coeffs.shape()[0];

  xt::xarray<double> _x = x;
  if (_x.dimension() == 1)
    _x.reshape({_x.shape(0), 1});

  // Tabulate
  std::vector<double> basis_data(ndsize * x.shape()[0] * ndofs * vs);
  tabulate(nd, _x, basis_data.data());

  // Pack data in
  xt::xtensor<double, 4> data({ndsize, _x.shape(0), ndofs, vs});

  // Loop over derivatives
  for (std::size_t d = 0; d < data.shape(0); ++d)
  {
    std::size_t offset_d = d * data.shape(1) * data.shape(2) * data.shape(3);

    // Loop over points
    for (std::size_t p = 0; p < data.shape(1); ++p)
    {
      // Loop over basis functions
      for (std::size_t r = 0; r < data.shape(2); ++r)
      {
        // Loop over values
        for (std::size_t i = 0; i < data.shape(3); ++i)
        {
          std::size_t offset = offset_d + p + r * data.shape(1)
                               + i * data.shape(1) * data.shape(2);
          assert(offset < basis_data.size());
          data(d, p, r, i) = basis_data[offset];
        }
      }
    }
  }

  return data;
}
//-----------------------------------------------------------------------------
void FiniteElement::tabulate(int nd, const xt::xarray<double>& x,
                             double* basis_data) const
{
  xt::xarray<double> _x = x;
  if (_x.dimension() == 2 and x.shape()[1] == 1)
    _x.reshape({x.shape()[0]});

  const std::size_t tdim = cell::topological_dimension(_cell_type);
  if (_x.shape()[1] != tdim)
    throw std::runtime_error("Point dim does not match element dim.");

  xt::xtensor<double, 3> basis = polyset::tabulate(_cell_type, _degree, nd, _x);
  const int psize = polyset::dim(_cell_type, _degree);
  const std::size_t ndofs = _coeffs.shape()[0];
  const int vs = value_size();
  xt::xtensor<double, 2> B, C;
  for (std::size_t p = 0; p < basis.shape()[0]; ++p)
  {
    // Map block for current derivative
    std::array<std::size_t, 2> shape = {_x.shape()[0], ndofs * vs};
    std::size_t offset = p * x.shape()[0] * ndofs * vs;
    auto dresult = xt::adapt<xt::layout_type::column_major>(
        basis_data + offset, x.shape()[0] * ndofs * vs, xt::no_ownership(),
        shape);
    for (int j = 0; j < vs; ++j)
    {
      B = xt::view(basis, p, xt::all(), xt::all());
      C = xt::transpose(xt::view(_coeffs, xt::all(),
                                 xt::range(psize * j, psize * j + psize)));
      xt::view(dresult, xt::range(0, x.shape()[0]),
               xt::range(ndofs * j, ndofs * j + ndofs))
          = xt::linalg::dot(B, C);
    }
  }
}
//-----------------------------------------------------------------------------
const xt::xtensor<double, 3>& FiniteElement::base_transformations() const
{
  return _base_transformations;
}
//-----------------------------------------------------------------------------
int FiniteElement::num_points() const { return _points.shape()[0]; }
//-----------------------------------------------------------------------------
const xt::xtensor<double, 2>& FiniteElement::points() const { return _points; }
//-----------------------------------------------------------------------------
xt::xtensor<double, 3> FiniteElement::map_push_forward(
    const xt::xtensor<double, 3>& U, const xt::xtensor<double, 3>& J,
    const tcb::span<const double>& detJ, const xt::xtensor<double, 3>& K) const
{
  const std::size_t physical_value_size
      = compute_value_size(_map_type, J.shape(1));
  xt::xtensor<double, 3> u({U.shape(0), U.shape(1), physical_value_size});
  map_push_forward_m(U, J, detJ, K, u);

  return u;
}
//-----------------------------------------------------------------------------
xt::xtensor<double, 3> FiniteElement::map_pull_back(
    const xt::xtensor<double, 3>& u, const xt::xtensor<double, 3>& J,
    const tcb::span<const double>& detJ, const xt::xtensor<double, 3>& K) const
{
  const std::size_t reference_value_size = value_size();
  xt::xtensor<double, 3> U({u.shape(0), u.shape(1), reference_value_size});
  map_pull_back_m(u, J, detJ, K, U);
  return U;
}
//-----------------------------------------------------------------------------
std::string basix::version()
{
  static const std::string version_str = str(BASIX_VERSION);
  return version_str;
}
//-----------------------------------------------------------------------------
int FiniteElement::compute_value_size(mapping::type map_type, int dim)
{
  switch (map_type)
  {
  case mapping::type::identity:
    return 1;
  case mapping::type::covariantPiola:
    return dim;
  case mapping::type::contravariantPiola:
    return dim;
  case mapping::type::doubleCovariantPiola:
    return dim * dim;
  case mapping::type::doubleContravariantPiola:
    return dim * dim;
  default:
    throw std::runtime_error("Mapping not yet implemented");
  }
}
//-----------------------------------------------------------------------------