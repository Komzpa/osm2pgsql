#ifndef OSMIUM_GEOM_FACTORY_HPP
#define OSMIUM_GEOM_FACTORY_HPP

/*

This file is part of Osmium (http://osmcode.org/libosmium).

Copyright 2013-2017 Jochen Topf <jochen@topf.org> and others (see README).

Boost Software License - Version 1.0 - August 17th, 2003

Permission is hereby granted, free of charge, to any person or organization
obtaining a copy of the software and accompanying documentation covered by
this license (the "Software") to use, reproduce, display, distribute,
execute, and transmit the Software, and to prepare derivative works of the
Software, and to permit third-parties to whom the Software is furnished to
do so, all subject to the following:

The copyright notices in the Software and this entire statement, including
the above license grant, this restriction and the following disclaimer,
must be included in all copies of the Software, in whole or in part, and
all derivative works of the Software, unless such copies or derivative
works are solely in the form of machine-executable object code generated by
a source language processor.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE, TITLE AND NON-INFRINGEMENT. IN NO EVENT
SHALL THE COPYRIGHT HOLDERS OR ANYONE DISTRIBUTING THE SOFTWARE BE LIABLE
FOR ANY DAMAGES OR OTHER LIABILITY, WHETHER IN CONTRACT, TORT OR OTHERWISE,
ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
DEALINGS IN THE SOFTWARE.

*/

#include <cstddef>
#include <stdexcept>
#include <string>
#include <utility>

#include <osmium/geom/coordinates.hpp>
#include <osmium/memory/collection.hpp>
#include <osmium/memory/item.hpp>
#include <osmium/osm/area.hpp>
#include <osmium/osm/item_type.hpp>
#include <osmium/osm/location.hpp>
#include <osmium/osm/node.hpp>
#include <osmium/osm/node_ref.hpp>
#include <osmium/osm/node_ref_list.hpp>
#include <osmium/osm/types.hpp>
#include <osmium/osm/way.hpp>

namespace osmium {

    /**
     * Exception thrown when an invalid geometry is encountered. An example
     * would be a linestring with less than two points.
     */
    class geometry_error : public std::runtime_error {

        std::string m_message;
        osmium::object_id_type m_id;

    public:

        explicit geometry_error(const std::string& message, const char* object_type = "", osmium::object_id_type id = 0) :
            std::runtime_error(message),
            m_message(message),
            m_id(id) {
            if (m_id != 0) {
                m_message += " (";
                m_message += object_type;
                m_message += "_id=";
                m_message += std::to_string(m_id);
                m_message += ")";
            }
        }

        void set_id(const char* object_type, osmium::object_id_type id) {
            if (m_id == 0 && id != 0) {
                m_message += " (";
                m_message += object_type;
                m_message += "_id=";
                m_message += std::to_string(id);
                m_message += ")";
            }
            m_id = id;
        }

        osmium::object_id_type id() const noexcept {
            return m_id;
        }

        const char* what() const noexcept override {
            return m_message.c_str();
        }

    }; // class geometry_error

    /**
     * @brief Everything related to geometry handling.
     */
    namespace geom {

        /**
         * Which nodes of a way to use for a linestring.
         */
        enum class use_nodes : bool {
            unique = true, ///< Remove consecutive nodes with same location.
            all    = false ///< Use all nodes.
        }; // enum class use_nodes

        /**
         * Which direction the linestring created from a way
         * should have.
         */
        enum class direction : bool {
            backward = true, ///< Linestring has reverse direction.
            forward  = false ///< Linestring has same direction as way.
        }; // enum class direction

        /**
         * This pseudo projection just returns its WGS84 input unchanged.
         * Used as a template parameter if a real projection is not needed.
         */
        class IdentityProjection {

        public:

            Coordinates operator()(osmium::Location location) const {
                return Coordinates{location.lon(), location.lat()};
            }

            int epsg() const noexcept {
                return 4326;
            }

            std::string proj_string() const {
                return "+proj=longlat +datum=WGS84 +no_defs";
            }

        }; // class IdentityProjection

        /**
         * Geometry factory.
         */
        template <typename TGeomImpl, typename TProjection = IdentityProjection>
        class GeometryFactory {

            /**
             * Add all points of an outer or inner ring to a multipolygon.
             */
            void add_points(const osmium::NodeRefList& nodes) {
                osmium::Location last_location;
                for (const osmium::NodeRef& node_ref : nodes) {
                    if (last_location != node_ref.location()) {
                        last_location = node_ref.location();
                        m_impl.multipolygon_add_location(m_projection(last_location));
                    }
                }
            }

            TProjection m_projection;
            TGeomImpl m_impl;

        public:

            /**
             * Constructor for default initialized projection.
             */
            template <typename... TArgs>
            explicit GeometryFactory<TGeomImpl, TProjection>(TArgs&&... args) :
                m_projection(),
                m_impl(m_projection.epsg(), std::forward<TArgs>(args)...) {
            }

            /**
             * Constructor for explicitly initialized projection. Note that the
             * projection is moved into the GeometryFactory.
             */
            template <typename... TArgs>
            explicit GeometryFactory<TGeomImpl, TProjection>(TProjection&& projection, TArgs&&... args) :
                m_projection(std::move(projection)),
                m_impl(m_projection.epsg(), std::forward<TArgs>(args)...) {
            }

            using projection_type   = TProjection;

            using point_type        = typename TGeomImpl::point_type;
            using linestring_type   = typename TGeomImpl::linestring_type;
            using polygon_type      = typename TGeomImpl::polygon_type;
            using multipolygon_type = typename TGeomImpl::multipolygon_type;
            using ring_type         = typename TGeomImpl::ring_type;

            int epsg() const noexcept {
                return m_projection.epsg();
            }

            std::string proj_string() const {
                return m_projection.proj_string();
            }

            /* Point */

            point_type create_point(const osmium::Location& location) const {
                return m_impl.make_point(m_projection(location));
            }

            point_type create_point(const osmium::Node& node) {
                try {
                    return create_point(node.location());
                } catch (osmium::geometry_error& e) {
                    e.set_id("node", node.id());
                    throw;
                }
            }

            point_type create_point(const osmium::NodeRef& node_ref) {
                try {
                    return create_point(node_ref.location());
                } catch (osmium::geometry_error& e) {
                    e.set_id("node", node_ref.ref());
                    throw;
                }
            }

            /* LineString */

            void linestring_start() {
                m_impl.linestring_start();
            }

            template <typename TIter>
            size_t fill_linestring(TIter it, TIter end) {
                size_t num_points = 0;
                for (; it != end; ++it, ++num_points) {
                    m_impl.linestring_add_location(m_projection(it->location()));
                }
                return num_points;
            }

            template <typename TIter>
            size_t fill_linestring_unique(TIter it, TIter end) {
                size_t num_points = 0;
                osmium::Location last_location;
                for (; it != end; ++it) {
                    if (last_location != it->location()) {
                        last_location = it->location();
                        m_impl.linestring_add_location(m_projection(last_location));
                        ++num_points;
                    }
                }
                return num_points;
            }

            linestring_type linestring_finish(size_t num_points) {
                return m_impl.linestring_finish(num_points);
            }

            linestring_type create_linestring(const osmium::WayNodeList& wnl, use_nodes un = use_nodes::unique, direction dir = direction::forward) {
                linestring_start();
                size_t num_points = 0;

                if (un == use_nodes::unique) {
                    osmium::Location last_location;
                    switch (dir) {
                        case direction::forward:
                            num_points = fill_linestring_unique(wnl.cbegin(), wnl.cend());
                            break;
                        case direction::backward:
                            num_points = fill_linestring_unique(wnl.crbegin(), wnl.crend());
                            break;
                    }
                } else {
                    switch (dir) {
                        case direction::forward:
                            num_points = fill_linestring(wnl.cbegin(), wnl.cend());
                            break;
                        case direction::backward:
                            num_points = fill_linestring(wnl.crbegin(), wnl.crend());
                            break;
                    }
                }

                if (num_points < 2) {
                    throw osmium::geometry_error{"need at least two points for linestring"};
                }

                return linestring_finish(num_points);
            }

            linestring_type create_linestring(const osmium::Way& way, use_nodes un=use_nodes::unique, direction dir = direction::forward) {
                try {
                    return create_linestring(way.nodes(), un, dir);
                } catch (osmium::geometry_error& e) {
                    e.set_id("way", way.id());
                    throw;
                }
            }

            /* Polygon */

            void polygon_start() {
                m_impl.polygon_start();
            }

            template <typename TIter>
            size_t fill_polygon(TIter it, TIter end) {
                size_t num_points = 0;
                for (; it != end; ++it, ++num_points) {
                    m_impl.polygon_add_location(m_projection(it->location()));
                }
                return num_points;
            }

            template <typename TIter>
            size_t fill_polygon_unique(TIter it, TIter end) {
                size_t num_points = 0;
                osmium::Location last_location;
                for (; it != end; ++it) {
                    if (last_location != it->location()) {
                        last_location = it->location();
                        m_impl.polygon_add_location(m_projection(last_location));
                        ++num_points;
                    }
                }
                return num_points;
            }

            polygon_type polygon_finish(size_t num_points) {
                return m_impl.polygon_finish(num_points);
            }

            polygon_type create_polygon(const osmium::WayNodeList& wnl, use_nodes un = use_nodes::unique, direction dir = direction::forward) {
                polygon_start();
                size_t num_points = 0;

                if (un == use_nodes::unique) {
                    osmium::Location last_location;
                    switch (dir) {
                        case direction::forward:
                            num_points = fill_polygon_unique(wnl.cbegin(), wnl.cend());
                            break;
                        case direction::backward:
                            num_points = fill_polygon_unique(wnl.crbegin(), wnl.crend());
                            break;
                    }
                } else {
                    switch (dir) {
                        case direction::forward:
                            num_points = fill_polygon(wnl.cbegin(), wnl.cend());
                            break;
                        case direction::backward:
                            num_points = fill_polygon(wnl.crbegin(), wnl.crend());
                            break;
                    }
                }

                if (num_points < 4) {
                    throw osmium::geometry_error{"need at least four points for polygon"};
                }

                return polygon_finish(num_points);
            }

            polygon_type create_polygon(const osmium::Way& way, use_nodes un=use_nodes::unique, direction dir = direction::forward) {
                try {
                    return create_polygon(way.nodes(), un, dir);
                } catch (osmium::geometry_error& e) {
                    e.set_id("way", way.id());
                    throw;
                }
            }

            /* MultiPolygon */

            multipolygon_type create_multipolygon(const osmium::Area& area) {
                try {
                    size_t num_polygons = 0;
                    size_t num_rings = 0;
                    m_impl.multipolygon_start();

                    for (auto it = area.cbegin(); it != area.cend(); ++it) {
                        if (it->type() == osmium::item_type::outer_ring) {
                            auto& ring = static_cast<const osmium::OuterRing&>(*it);
                            if (num_polygons > 0) {
                                m_impl.multipolygon_polygon_finish();
                            }
                            m_impl.multipolygon_polygon_start();
                            m_impl.multipolygon_outer_ring_start();
                            add_points(ring);
                            m_impl.multipolygon_outer_ring_finish();
                            ++num_rings;
                            ++num_polygons;
                        } else if (it->type() == osmium::item_type::inner_ring) {
                            auto& ring = static_cast<const osmium::InnerRing&>(*it);
                            m_impl.multipolygon_inner_ring_start();
                            add_points(ring);
                            m_impl.multipolygon_inner_ring_finish();
                            ++num_rings;
                        }
                    }

                    // if there are no rings, this area is invalid
                    if (num_rings == 0) {
                        throw osmium::geometry_error{"invalid area"};
                    }

                    m_impl.multipolygon_polygon_finish();
                    return m_impl.multipolygon_finish();
                } catch (osmium::geometry_error& e) {
                    e.set_id("area", area.id());
                    throw;
                }
            }

        }; // class GeometryFactory

    } // namespace geom

} // namespace osmium

#endif // OSMIUM_GEOM_FACTORY_HPP
